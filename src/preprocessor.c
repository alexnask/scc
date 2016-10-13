#include <preprocessor.h>
#include <macros.h>

static void skip_whitespace(size_t *index, pp_token_vector *vec) {
    pp_token *tokens = vec->memory;

    for (; *index < vec->size && tokens[*index].kind == PP_TOK_WHITESPACE; (*index)++) {}
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
        } else if (!IS("define") && !IS("include") && !IS("pragma") && !IS("line") && !IS("line") && !IS("error")) {
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
    }

    state->line.line++;

    return result;
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
