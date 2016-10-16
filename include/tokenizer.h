#ifndef TOKENIZER_H__
#define TOKENIZER_H__

#include <strings.h>
#include <sc_io.h>

// TODO: Remove whitespace token type, add a has_whitespace flag on the token types.
typedef enum pp_token_kind {
    PP_TOK_WHITESPACE,
    PP_TOK_HEADER_NAME,
    PP_TOK_IDENTIFIER,
    PP_TOK_NUMBER,
    PP_TOK_CHAR_CONST,
    PP_TOK_STR_LITERAL,
    // Punctuators
    PP_TOK_HASH,
    PP_TOK_DOUBLEHASH,
    PP_TOK_DOT,
    PP_TOK_ARROW,
    PP_TOK_COMMA,
    PP_TOK_QUESTION_MARK,
    PP_TOK_ASSIGN,
    PP_TOK_PLUS,
    PP_TOK_PLUS_ASSIGN,
    PP_TOK_MINUS,
    PP_TOK_MINUS_ASSIGN,
    PP_TOK_STAR,
    PP_TOK_STAR_ASSIGN,
    PP_TOK_DIV,
    PP_TOK_DIV_ASSIGN,
    PP_TOK_MOD,
    PP_TOK_MOD_ASSIGN,
    PP_TOK_INCREMENT,
    PP_TOK_DECREMENT,
    PP_TOK_EQUALS,
    PP_TOK_NOT_EQUALS,
    PP_TOK_GREATER,
    PP_TOK_GREATER_EQUALS,
    PP_TOK_LESS,
    PP_TOK_LESS_EQUALS,
    PP_TOK_LOGICAL_NOT,
    PP_TOK_LOGICAL_AND,
    PP_TOK_LOGICAL_OR,
    PP_TOK_BITWISE_NOT,
    PP_TOK_BITWISE_AND,
    PP_TOK_BITWISE_AND_ASSIGN,
    PP_TOK_BITWISE_OR,
    PP_TOK_BITWISE_OR_ASSIGN,
    PP_TOK_BITWISE_XOR,
    PP_TOK_BITWISE_XOR_ASSIGN,
    PP_TOK_LEFT_SHIFT,
    PP_TOK_LEFT_SHIFT_ASSIGN,
    PP_TOK_RIGHT_SHIFT,
    PP_TOK_RIGHT_SHIFT_ASSIGN,
    PP_TOK_OPEN_SQUARE_BRACKET,
    PP_TOK_CLOSE_SQUARE_BRACKET,
    PP_TOK_OPEN_BRACKET,
    PP_TOK_CLOSE_BRACKET,
    PP_TOK_OPEN_PAREN,
    PP_TOK_CLOSE_PAREN,
    PP_TOK_SEMICOLON,
    PP_TOK_COLON,
    PP_TOK_OTHER,
    // Cannot be read.
    PP_TOK_PLACEMARKER,
    // '##' resulting from a token concatenation, which is not actually an operator.
    PP_TOK_CONCAT_DOUBLEHASH
} pp_token_kind;

typedef struct pp_token {
    pp_token_kind kind;

    struct {
        const char *path;
        size_t line;
        size_t column;
    } source;

    string data;
    bool has_whitespace;
} pp_token;

// TODO: Destroy function.

// true on success, false on failure.
// Must result in a single preprocessing token.
bool pp_token_concatenate(pp_token *dest, pp_token *left, pp_token *right);
void pp_token_copy(pp_token *dest, pp_token *src);

typedef struct tokenizer_state {
    // File path
    const char *path;

    // Current column and line at the start and end of the current chunk.
    size_t line_start;
    size_t column_start;
    size_t line_end;
    size_t column_end;

    // Pointer to our constant file data.
    const char *data;
    // Current index + whole size.
    size_t index;
    size_t data_size;

    // Data after initial processing that we are tokenizing.
    string current_data;
    // How many bytes out of the current_data have been processed.
    size_t done;

    // Used for error reporting.
    struct {
        size_t index;
        size_t line;
        size_t column;
    } multiline_source;

    bool in_multiline_comment;
    bool in_include;
} tokenizer_state;

void tokenizer_state_init(tokenizer_state *state, sc_file_cache_handle handle);

struct pp_token_vector;
bool tokenize_line(struct pp_token_vector *vec, tokenizer_state *state);

// TODO: Token type
// With these source kinds: file, define
// Will have a source stack to track origins.

typedef struct token_source {
    enum {
        TSRC_FILE,
        TSRC_INCLUDE,
        TSRC_MACRO
    } kind;

    union {
        struct {
            // Path to the file.
            string path;
            // Line and column the token originates from.
            size_t line;
            size_t column;
        } file;

        struct {
            // Include path.
            string path;
            // Line and column of the include.
            size_t line;
            size_t column;
        } include;

        struct {
            // Macro name
            string name;
            // Line and column of the definition of the macro.
            size_t line;
            size_t column;
        } macro;
    };
} token_source;

typedef enum token_kind {
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    // TODO -> Number literals,
    TOK_CHAR_CONST,
    TOK_STR_LITERAL,
    TOK_DOT,
    TOK_ARROW,
    TOK_COMMA,
    TOK_QUESTION_MARK,
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_PLUS_ASSIGN,
    TOK_MINUS,
    TOK_MINUS_ASSIGN,
    TOK_STAR,
    TOK_STAR_ASSIGN,
    TOK_DIV,
    TOK_DIV_ASSIGN,
    TOK_MOD,
    TOK_MOD_ASSIGN,
    TOK_INCREMENT,
    TOK_DECREMENT,
    TOK_EQUALS,
    TOK_NOT_EQUALS,
    TOK_GREATER,
    TOK_GREATER_EQUALS,
    TOK_LESS,
    TOK_LESS_EQUALS,
    TOK_LOGICAL_NOT,
    TOK_LOGICAL_AND,
    TOK_LOGICAL_OR,
    TOK_BITWISE_NOT,
    TOK_BITWISE_AND,
    TOK_BITWISE_AND_ASSIGN,
    TOK_BITWISE_OR,
    TOK_BITWISE_OR_ASSIGN,
    TOK_BITWISE_XOR,
    TOK_BITWISE_XOR_ASSIGN,
    TOK_LEFT_SHIFT,
    TOK_LEFT_SHIFT_ASSIGN,
    TOK_RIGHT_SHIFT,
    TOK_RIGHT_SHIFT_ASSIGN,
    TOK_OPEN_SQUARE_BRACKET,
    TOK_CLOSE_SQUARE_BRACKET,
    TOK_OPEN_BRACKET,
    TOK_CLOSE_BRACKET,
    TOK_OPEN_PAREN,
    TOK_CLOSE_PAREN,
    TOK_SEMICOLON,
    TOK_COLON
} token_kind;

typedef struct token {
    token_kind kind;

    // Copied over from preprocessing tokens.
    string data;

    token_source *source_stack;
    size_t stack_size;

    // Set by #line directive.
    struct {
        string path;
        size_t line;
    } line;

    // Is the token followed by whitespace?
    bool has_whitespace;
} token;

#endif
