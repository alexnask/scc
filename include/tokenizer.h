#ifndef TOKENIZER_H__
#define TOKENIZER_H__

#include <sc_io.h>

// The goal of this API is to be able to build a streaming interface for the preprocessor.
// That way, memory allocations are kept to a minimum.
// Ideally, we will be working with a maximum of three token vectors.
// One will be the final translation unit, another will be the current macro expansion (small) vector and the final one will be the temporary macro expansion (small) vector.
// Tokens will be pushed to the translation unit vector and when we hit a macro expansion, the will be pushed to the macro expansion vector.
// Then, the temporary and macro expansion vectors are swapped, the (new) macro expansion vector is cleared and we expand the (new) temporary vector into the (new) macro expansion vector.
// This proccess is repeated until there are no expansions left.
// At that point, the final macro expansion vector is pushed into the translation unit vector and we keep on pulling tokens.

typedef enum token_kind {
    TOK_COMMENT,
    TOK_WHITESPACE,
    TOK_NEWLINE,
    TOK_SLASH,
    TOK_SLASHASSIGN,
    TOK_OPENPAREN,
    TOK_CLOSEPAREN,
    TOK_STAR,
    TOK_STARASSIGN,
    TOK_PLUS,
    TOK_PLUSASSIGN,
    TOK_INCREMENT,
    TOK_DECREMENT,
    TOK_MINUS,
    TOK_MINUSASSIGN,
    TOK_MODULO,
    TOK_MODULOASSIGN,
    TOK_LESSTHAN,
    TOK_LESSTHANEQ,
    TOK_GREATERTHAN,
    TOK_GREATERTHANEQ,
    TOK_NEQUALS,
    TOK_BANG,
    TOK_EQUALS,
    TOK_ASSIGN,
    TOK_COLON,
    TOK_HASH,
    TOK_DOUBLEHASH,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_DOT,
    TOK_ARROW,
    TOK_QUESTIONMARK,
    TOK_OPENSQBRACK,
    TOK_CLOSESQBRACK,
    TOK_BITWISEXOR,
    TOK_BITWISEXORASSIGN,
    TOK_OPENBRACK,
    TOK_CLOSEBRACK,
    TOK_BITWISEOR,
    TOK_BITWISEORASSIGN,
    TOK_BITWISENOT,
    TOK_LOGICALOR,
    TOK_BITWISEAND,
    TOK_BITWISEANDASSIGN,
    TOK_LOGICALAND,
    TOK_RIGHTSHIFT,
    TOK_RIGHTSHIFTASSIGN,
    TOK_LEFTSHIFT,
    TOK_LEFTSHIFTASSIGN,
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_CHARACTERLITERAL,
    TOK_STRINGLITERAL,
    TOK_DECIMALINTEGERLITERAL,
    TOK_HEXADECIMALINTEGERLITERAL,
    TOK_OCTALINTEGERLITERAL,
    TOK_DECIMALFLOATLITERAL,
    TOK_HEXADECIMALFLOATLITERAL,
    TOK_EOF
} token_kind;

// TODO: Multiple types of token_sources
// (file, macro)
typedef struct token_source {
    sc_file_cache_handle source_file;
    long int offset;
    long int size;

    size_t line;
    size_t column;

    // Set by #line
    const char *path_overwrite;
} token_source;

typedef struct token {
    token_source source;
    token_kind kind;
} token;

typedef struct tokenizer_state {
    sc_file_cache_handle source_handle;
    long int current_index;

    size_t current_line;
    size_t current_column;
} tokenizer_state;

void tokenizer_state_init(tokenizer_state *state, sc_file_cache_handle handle);

void next_token(token *token, tokenizer_state *state);

char *token_data(token *token);
long int token_size(token *token);

// Notes:
// String literals must be escaped, check for "\\\n" and remove it completely and replace triglyphs with their respective symbols.

#endif
