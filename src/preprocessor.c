#include <preprocessor.h>
#include <macros.h>

static void push_token(size_t i, preprocessor_state *state);

static bool is_keyword(string *data) {
    #define IS(L) STRING_EQUALS_LITERAL(data, L)
    // Get ready for a huge return statement.
    return IS("auto") || IS("break") || IS("case") || IS("char") || IS("const") || IS("continue") || IS("default") || IS("do")
        || IS("double") || IS("else") || IS("enum") || IS("extern") || IS("float") || IS("for") || IS("goto") || IS("if")
        || IS("inline") || IS("int") || IS("long") || IS("register") || IS("restrict") || IS("return") || IS("short")
        || IS("signed") || IS("sizeof") || IS("static") || IS("struct") || IS("switch") || IS("typedef") || IS("union")
        || IS("unsigned") || IS("void") | IS("volatile") || IS("while") || IS("_Alignas") || IS("_Alignof") || IS("_Atomic")
        || IS("_Bool") || IS("_Complex") || IS("_Generic") || IS("_Imaginary") || IS("_Noreturn") || IS("_Static_assert")
        || IS("_Thread_local");
    #undef IS
}

static bool skip_whitespace(size_t *index, pp_token_vector *vec) {
    pp_token *tokens = vec->memory;
    size_t start_idx = *index;

    for (; *index < vec->size && tokens[*index].kind == PP_TOK_WHITESPACE; (*index)++) {}
    return *index != start_idx;
}

static void add_branch(preprocessor_state *state, size_t nesting, bool ignoring) {
    if (state->branch_stack.size >= state->branch_stack.capacity) {
        // Just have a couple floating.
        state->branch_stack.capacity = state->branch_stack.size + 2;
        state->branch_stack.memory = realloc(state->branch_stack.memory, state->branch_stack.capacity * sizeof(pp_branch));
    }

    state->branch_stack.memory[state->branch_stack.size++] = (pp_branch) { .nesting = nesting, .ignoring = ignoring };
}

static void pop_branch(preprocessor_state *state) {
    assert(state->branch_stack.size > 0);
    state->ignore_stack.size--;
}

static size_t latest_branch_nesting(preprocessor_state *state) {
    assert(state->branch_stack.size > 0);
    return state->branch_stack.memory[state->branch_stack.size - 1].nesting;
}

static void latest_branch_flip(preprocessor_state *state) {
    assert(state->branch_stack.size > 0);
    state->branch_stack.memory[state->branch_stack.size - 1].nesting = !state->branch_stack.memory[state->branch_stack.size - 1].nesting;
}

static bool ignoring(preprocessor_state *state) {
    return state->ignore_stack.size > 0 && state->ignore_stack.memory[state->ignore_stack.size - 1].ignoring;
}

static void do_ifdef(bool must_be_defined, size_t index, preprocessor_state *state) {
    assert(!ignoring(state));

    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    state->if_nesting++;

    skip_whitespace(&index, vec);

    if (index == vec->size || tokens[index].kind != PP_TOK_IDENTIFIER) {
        sc_error(false, "Expected macro name after #%s", must_be_defined ? "ifdef" : "ifndef");
        return;
    }

    // TODO: Handle builtins (in define_exists)
    add_branch(state, state->if_nesting - 1, define_exists(&state->def_table, &tokens[index].data) != must_be_defined);

    // Check for extra tokens
    index++;
    if (index != vec->size) {
        sc_error(false, "Expected only macro name after #%s", must_be_defined ? "ifdef" : "ifndef");
    }
}

// TODO: Many of the skip_whitespace's NEED to happen
// Make it return a bool and check against the return.
static void handle_directive(size_t index, preprocessor_state *state) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    if (tokens[index].kind != PP_TOK_IDENTIFIER) {
        // TODO: Good errors.
        sc_error(false, "Non-identifier directive...");
        return;
    }

    string *directive = &tokens[index].data;

    #define IS(L) STRING_EQUALS_LITERAL(directive, L)

    // TODO: These don't actually work for nested conditions all the time :/
    // We should probably have some stack of ignored-until-nestings.
    if (IS("endif")) {
        // If nesting being zero here is impossible, since we are still ignoring.
        state->if_nesting--;
        if (state->if_nesting == latest_branch_nesting(state)) {
            pop_branch(state);
        }

        index++;
        if (index != vec->size) {
            sc_error(true, "#endif expected no parameters");
        }
        return;
    } else if (IS("else")) {
        if (state->if_nesting == 0) {
            sc_error(false, "#else without condition.");
            return;
        }

        // TODO: Check if + 1 is correct.
        if (state->if_nesting == latest_branch_nesting(state) + 1) {
            latest_branch_flip(state);
        }

        index++;
        if (index != vec->size) {
            sc_error(true, "#else expected no parameters");
        }
        return;
    } else if (IS("elif")) {
        // TODO: Do stuff here
    }

    if (ignoring(state)) {
        if (IS("ifndef") || IS("if") || IS("ifdef")) {
            state->if_nesting++;
            // Check for superfluous tokens here.
        } else if (IS("elif")) {
            // Stop ignoring if at correct nesting and condition is true
            // Check for superfluous tokens.
        } else if (!IS("define") && !IS("include") && !IS("pragma") && !IS("line") && !IS("line") && !IS("error") && !IS("undef")) {
            sc_error(false, "Unknown directive '%s'", string_data(directive));
            return;
        }
    } else {
        if (IS("ifdef")) {
            index++;
            do_ifdef(true, index, state);
        } else if (IS("ifndef")) {
            index++;
            do_ifdef(false, index, state);
        } else if (IS("if")) {
            // TODO;
        } else if (IS("elif")) {
            // TODO;
        }
        // TODO: Add rest of directives
        // TODO: Error on unknown directive
        else if (IS("error")) {
            index++;
            skip_whitespace(&index, vec);
            // TODO: ERROR REPORTING
            string str;
            string_init(&str, 0);
            for (size_t i = 0; i < vec->size; i++) {
                string_append(&str, &tokens[i].data);
            }
            sc_error(false, string_data(str));
            string_destroy(&str);
        } else if (IS("line")) {
            index++;
            skip_whitespace(&index, vec);

            if (tokens[index].kind != PP_TOK_NUMBER) {
                sc_error(false, "Expected line number after #line directive.");
                return;
            }

            // Steal the token's data.
            const char *num_data = string_data(tokens[index].data);
            // Parse it to an integer, if we can.
            for (size_t i = 0; i < string_size(num); i++) {
                if (!isdigit(num_data[i])) {
                    sc_error(false, "Expected a decimal line number in #line directive.");
                    return;
                }
            }

            // TODO: Write our own, better version (with bounds/error checking, faster [see folly talk])...
            state->line.line = strtoull(num_data);

            index++;
            skip_whitespace(&index, vec);
            if (index != vec->size) {
                if (tokens[index].kind != PP_TOK_STR_LITERAL) {
                    sc_error(false, "Expected a string literal as a second argument of the #line directive.");
                    return;
                }

                // Remove quotes
                // TODO: Unescape this.
                substring(&state->line.path, &tokens[index].data, 1, -1);
                index++;
                if (index != vec->size) {
                    sc_error(false, "#line directive can have two arguments at most.");
                    return;
                }
            }
        } else if (IS("undef")) {
            index++;
            skip_whitespace(&index, vec);

            if (index == vec->size || tokens[index].kind != PP_TOK_IDENTIFIER) {
                sc_error(false, "Expected macro name as argument of #undef.");
                return;
            }

            define *entry = define_table_lookup(&state->def_table, &tokens[index].data);
            if (entry && entry->active) {
                entry->active = false;
            } else {
                sc_warning("Called #undef on non-defined macro '%s'", string_data(&toknes[index].data));
            }
        }
    }

    #undef IS
}

bool preprocess_line(preprocessor_state *state) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    vec->size = 0;

    bool result = tokenize_line(state->line_vec, state->tok_state);

    // Skip any leading and trailing whitespace.
    size_t idx;
    // Leading whitespace.
    for (idx = 0; idx < vec->size && tokens[idx].kind == PP_TOK_WHITESPACE; idx++) {}
    // Trailing whitespace.
    for (; vec->size >= 0 && tokens[vec->size - 1].kind == PP_TOK_WHITESPACE; vec->size--) {}

    if (tokens[idx].kind == PP_TOK_HASH) {
        // Preprocessor directive.
        idx++;

        skip_whitespace(&idx, vec);

        // No op.
        if (idx == vec->size) {
            return result;
        }

        handle_directive(idx, state);
    } else if (!ignoring(state)) {
        // Do stuff (pass over pp_tokens into tokens, check for macro substitution)
        // Also handle _Pragma
        // Also, pass #line thingies into the tokens.

        // Don't do substitution or _Pragma's for now.
        // Just pass along the tokens.
        for (size_t i = idx; i < vec->size; i++) {
            push_token(i, state);
        }
    }

    state->line.line++;

    return result;
}

void push_token(size_t i, preprocessor_state *state) {
    assert(i < state->line_vec->size);
    pp_token *src = state->line_vec->memory[i];
    token *dest = token_vector_tail(state->translation_unit);

    assert(src->kind != PP_TOK_HEADER_NAME && src->kind != PP_TOK_PLACEMARKER);
    if (src->kind == PP_TOK_OTHER || src->kind == PP_TOK_HASH || src->kind == PP_TOK_DOUBLEHASH) {
        sc_error(false, "Token '%s' made it out of preprocessing...", string_data(&src->data));
        return;
    }

    // Ok, lets pass over our current sources and add the default file one.
    dest->stack_size = state->source_stack.stack_size + 1;
    dest->source_stack = malloc(dest->stack_size * sizeof(token_source));

    memcpy(dest->source_stack, state->source_stack.memory, state->source_stack.stack_size * sizeof(token_source));
    dest->source_stack[state->source_stack.stack_size] = (token_source) {
        .kind = TSRC_FILE,
        .file.line = src->source.line,
        .file.column = src->source.column
    };
    // TODO: Number parsing, string and character escaping and other fun stuff.
    string_from_ptr_size(&dest->source_stack[state->source_stack.stack_size].file.path, str->source.path, strlen(str->source.path));
    string_copy(&dest->data, &src->data);

    // Pass over #line set stuff.
    string_copy(&dest->line.path, &state->line.path);
    dest->line.line = state->line.line;

    // Punctuators.
    if (src->kind >= PP_TOK_DOT && src->kind <= PP_TOK_COLON) {
        dest->kind = (src->kind - PP_TOK_DOT) + TOK_DOT;
    } else if (src->kind == PP_TOK_IDENTIFIER) {
        // Ok, let's break it DOWN.
        if (is_keyword(&dest->data)) {
            dest->kind = TOK_KEYWORD;
        } else {
            dest->kind = TOK_IDENTIFIER;
        }
    }
    // TODO: Other stuff
}

void preprocessor_state_init(preprocessor_state *state, tokenizer_state *tok_state, token_vector *translation_unit, pp_token_vector *line_vec) {
    state->tok_state = tok_state;
    state->translation_unit = translation_unit;
    state->line_vec = line_vec;

    // Default starting stack of 32 elements.
    state->source_stack.memory = malloc(32 * sizeof(token_source));
    state->source_stack.size = 0;
    state->source_stack.capacity = 32;

    state->if_nesting = 0;

    state->branch_stack.memory = malloc(8 * sizeof(pp_branch));
    state->branch_stack.size = 0;
    state->branch_stack.capacity = 8;

    define_table_init(&state->def_table);

    string_init(&state->line.path, 0);
    state->line.line = 1;
}
