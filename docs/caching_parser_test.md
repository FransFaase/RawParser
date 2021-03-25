# Test of the caching parser

Below the adapted version of the `test_parse_white_space` 
from [the test of the simple parser](simple_parser_test.md), which
initializes a memoization cache and assigns the memoization caching
function, before calling the parser on the input.
```c
void test_parse_white_space(non_terminal_dict_p all_nt, const char *input)
{
	text_buffer_t text_buffer;
	text_buffer_assign_string(&text_buffer, input);

	solutions_t solutions;
	solutions_init(&solutions, &text_buffer);

	parser_t parser;
	parser_init(&parser, &text_buffer);
	parser.cache.get_cache_item = solutions_find;
	parser.cache.cache_data = &solutions;
	
	if (parse_nt(&parser, find_nt("white_space", &all_nt)) && text_buffer_end(&text_buffer))
	{
		fprintf(stderr, "OK: parsed white space\n");
	}
	else
	{
		fprintf(stderr, "ERROR: failed to parse white space from '%s'\n", input);
	}
	
	solutions_free(&solutions);
}
```

## Testing with MarkDownC

With the following call to [MarkDownC](https://github.com/FransFaase/IParse/blob/master/README.md#markdownc)
a C-program can be generated to execute the test:
```
MarkDownC grammar.md simple_parser.md cached_parser.md simple_parser_test.md cached_parser_test.md text_buffer_impl.md >testsp.c
```
This will combine [the representation of the grammar](grammar.md), [the simple parser](simple_parser),
and [an implementation for a text buffer](text_buffer_impl.md) with the contents of this page into the
`testsp.c` C-program, which when run after being compiled, performs the test in this page.
