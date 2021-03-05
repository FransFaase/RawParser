# Test of the simple parser

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
