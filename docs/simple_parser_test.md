# Test of the simple parser

The function `test_parse_white_space` tries to parse the input in `input` with
non-terminal named 'white_space' in the grammar in `all_nt`.
```c
void test_parse_white_space(non_terminal_dict_p all_nt, const char *input)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);
	
	if (parse_nt(&text_buffer, find_nt("white_space", &all_nt)) && text_buffer_end(&text_buffer))
	{
		fprintf(stderr, "OK: parsed white space\n");
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse white space from '%s'\n", input);
	}
}
```

The function `test_white_space_grammer` contains the test of the white space
grammar (given an example in the section [Defines for defining a grammar](grammar.md#defines-for-defining-a-grammar))
and the `main` function contains the call to this function.
```c
void test_white_space_grammar()
{
	non_terminal_dict_p all_nt = NULL;

	white_space_grammar(&all_nt);
	test_parse_white_space(all_nt, " ");
	test_parse_white_space(all_nt, "/* */");
}

int main(int argc, char *argv[])
{
	test_white_space_grammar();
}
```

## Testing with MarkDownC

With the following call to [MarkDownC](https://github.com/FransFaase/IParse/blob/master/README.md#markdownc)
in the `docs` folder, a C-program can be generated to execute the test:
```
MarkDownC grammar.md simple_parser.md simple_parser_test.md text_buffer_impl.md >testsp.c
```
This will combine [the representation of the grammar](grammar.md), [the simple parser](simple_parser),
and [an implementation for a text buffer](text_buffer_impl.md) with the contents of this page into the
`testsp.c` C-program, which when run after being compiled, performs the test in this page.
