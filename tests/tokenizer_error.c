// !shouldfail
// Error:
// tests/tokenizer_error.c:2:10: Error: Relative include not closed on its line.
//                #include "some_file
//                         ~~~~~~~~


#include "some_file

int woops_that_include();
