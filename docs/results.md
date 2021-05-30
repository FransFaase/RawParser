# Results

The purpose of parsing, is not only to check whether some input
matches a given grammar, but to determine the semantic meaning
of the input. One way of doing this, is to construct a [abstract syntax
tree](https://en.wikipedia.org/wiki/Abstract_syntax_tree). The
idea is that each parsed rule results in some data based on the
data resulted from parsing its elements.

Because the parser is an interpreter, it means that all results
should be represented with the same type, which is some kind of
representation of the union of all possible result types. There are
different ways how this can be implemented, but for now it is
sufficient to have the following type define:
```c
typedef struct result result_t, *result_p;
```

It is possible that some reference counting will be used for the
result and for that reasons two macro symbols are used to indicate
the declaration of a local variable (`DECL_RESULT`) and the
disposal of it (`DISP_RESULT`). We also need a macro for assigning
a result from one value to the other (`ASS_RESULT`). With this we
can now adapt the previously defined parsing functions.

## Store results in the cache

When caching is used, each cache item should also store the result
of the parsing (in case it was successful). For this reason we
extend the cache item with a result, in the following manner:
```c
struct cache_item
{
    ...
	result_t result;         /* If it could be parsed, what result did it produce */
};
```

## Parse a non-terminal

Below the adapted function definition for [parsing a non-terminal](caching_parser.md#parse-a-non-terminal)
is given. In this the parameter `result` contains the result of parsing the non-terminal.
There are some more modification to the code, which are noted as such:
```c
bool parse_nt(parser_p parser, non_terminal_p non_term, result_p result)
{
    text_pos_t start_pos = parser->text_buffer->pos;

    cache_item_p cache_item = NULL;
    if (parser->cache.get_cache_item != 0)
    {
        cache_item = parser->cache.get_cache_item(&parser->cache, &parser->text_buffer->pos, non_term);
        if (cache_item != NULL)
        {
            if (cache_item->success == s_success)
            {
```
* The result of parsing the non-terminal is the result stored in the cache item:
```c
				ASS_RESULT(result, &cache_item->result);
                text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
                return TRUE;
            }
            else if (cache_item->success == s_fail)
            {
                return FALSE;
            }
            cache_item->success = s_fail;
        }
    }

    bool parsed_a_rule = FALSE;
    for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next)
    {
```
* The function to parse a rule, requires a parameter holding the result of the
  part of the rule that has been parsed. For this purpose the `start` result is
  declared and passed to the call of `parse_rule`.
```c
	    DECL_RESULT(start)
        if (parse_rule(parser, rule->elements, &start, rule, result))
        {
		    DISP_RESULT(start)
            parsed_a_rule = TRUE;
            break;
        }
		DISP_RESULT(start)
    }

    if (!parsed_a_rule)
    {
        if (cache_item != NULL)
        {
            if (parser->cache.set_result != 0)
                parser->cache.set_result(cache_item, &parser->cache, &start_pos, s_fail, NULL);
            else
                cache_item->success = s_fail;
        }
        return FALSE;
    }

    while (parsed_a_rule)
    {
        parsed_a_rule = FALSE;
        for (rules_p rule = non_term->recursive; rule != NULL; rule = rule->next)
        {
```
* Similar to the above, for the left-recursive rules, we also need to pass
  a value to represent the previous part of the rule. Because this will usually
  involve the result of what was parsed before, we add a call to the function
  `initialize_left_recusive_start_value`, which will be detailed below.
```c
			DECL_RESULT(start)
			initialize_left_recusive_start_value(result, rule, &start);
            if (parse_rule(parser, rule->elements, &start, rule, result))
            {
			    DISP_RESULT(start)
                parsed_a_rule = TRUE;
                break;
            }
			DISP_RESULT(start)
        }
    }

    if (cache_item != NULL)
    {
```
* The result of the parsed
    	ASS_RESULT(&cache_item->result, result);
        if (parser->cache.set_result != 0)
            parser->cache.set_result(cache_item, &parser->cache, &start_pos, s_success, &parser->text_buffer->pos);
        else
        {
            cache_item->success = s_success;
            cache_item->next_pos = parser->text_buffer->pos;
        }
    }

    return TRUE;
}
```

The above presume the following function:
```c
void initialize_left_recusive_start_value(result_p prev_result, rule_p rule, result_p start);
```

## Parse a rule

Below the adapted function definition for [The `parse_rule` function](greedy_modifier.md#parse-a-rule)
is given. Three parameters have been added. The parameter `prev_result` will contain the value of
part of the rule that already has been parsed. The parameter `rule` contains the definition of the
rule, which is needed for processing the previous results into the final result. The parameter
`rule_result` will contain the result when the input is parsed succesful by the rule.
There are some more modification to the code, which are noted as such:
```c
bool parse_rule(parser_p parser, element_p element, result_p prev_result, rules_p rule, result_p rule_result)
{
    for (; element != NULL && element->greedy; element = element->next)
    {
    	DECL_RESULT(elem_result);
        if (element->sequence)
        {
	    	DECL_RESULT(seq_begin);
	    	init_seq_begin(prev_result, rule, &seq_begin);
	    	
	    	DECL_RESULT(seq_elem);
	        if (parse_element(parser, element, &seq_begin, &seq_elem))
	        {
	        	DECL_RESULT(next_seq_elem);
                while (parse_element(parser, element, &seq_elem, &next_seq_elem))
                {
                	ASS_RESULT(seq_elem, next_seq_elem);
                }
            }
	        if (element->optional)
	        {
	        }
	        else
	        {
	        	DISP_RESULT(seq_elem);
	        	DISP_RESULT(seq_begin);
	        	DISP_RESULT(elem_result);
	            return FALSE;
	        }
        	DISP_RESULT(seq_elem);
        	DISP_RESULT(seq_begin);
        }
        else
        {
	        if (parse_element(parser, element))
	        {
	            if (element->sequence)
	            {
	                while (parse_element(parser, element)
	                {
	                }
	            }
	        }
	        if (element->optional)
	        {
	        }
	        else
	        {
	        	DISP_RESULT(elem_result);
	            return FALSE;
	        }
	    }
	    ASS_RESULT(prev_result, elem_result);
        DISP_RESULT(elem_result);
    }

    if (element == NULL)
    {
```
* The whole rule has been parsed. The function `create_rule_result` is called to
  create the result of the rule from the result of the parsed elements.
```c
        create_rule_result(prev_result, rule, rule_result)
        return TRUE;
    }

    if (element->optional && element->avoid)
    {
    	DECL_RESULT(skip_result);
    	add_skip_to(prev_result, element, @skip_result);
        if (parse_rule(parser, element->next, &skip_result, rule, result))
        {
        	DISP_RESULT(skip_result);
            return TRUE;
        }
        DISP_RESULT(skip_result);
    }

    text_pos_t sp = parser->text_buffer->pos;

    if (element->sequence)
    {
    	DECL_RESULT(seq_begin);
    	init_seq_begin(prev_result, rule, &seq_begin);
    	
    	DECL_RESULT(seq_elem);
	    if (parse_element(parser, element, &seq_begin, &seq_elem))
	    {
            if (parse_seq(parser, element, &seq_elem, prev_result, rule, result))
            {
            	DISP_RESULT(seq_elem);
            	DISP_RESULT(seq_begin);
                return TRUE;
            }
	    }
	    DISP_RESULT(seq_elem);
	    DISP_RESULT(seq_begin);
	}
	else
	{
		DECL_RESULT(elem_result);
	    if (parse_element(parser, element, prev_result, &elem_result))
	    {
            if (parse_rule(parser, element->next, elem_result, rule, result))
            {
            	DISP_RESULT(elem_result);
                return TRUE;
            }
	    }
	    DISP_RESULT(elem_result);
	}
    text_buffer_set_pos(parser->text_buffer, &sp);

    if (element->optional && !element->avoid)
    {
    	DECL_RESULT(skip_result);
    	add_skip_to(prev_result, element, @skip_result);
        if (parse_rule(parser, element->next, &skip_result, rule, result))
        {
        	DISP_RESULT(skip_result);
            return TRUE;
        }
        DISP_RESULT(skip_result);
    }

    return FALSE;
}
```
The above presumes the following functions:
```c
void add_skip_to(result_p prev_result, element_p element, result_p skip_result);
void init_seq_result(result_p prev_result, element_p element, result_p seq_begin);
```

## Parse a sequence

[The `parse_seq` function](simple_parser.md#parse-a-sequence) is only adapted with
respect to the usage of the parser structure.
```c
bool parse_seq(parser_p parser, element_p element)
{
    if (element->avoid)
    {
        if (parse_rule(parser, element->next))
            return TRUE;
    }

    text_pos_t sp = parser->text_buffer->pos;

    if (element->chain_rule == NULL || parse_rule(parser, element->chain_rule))
    {
        /* Try to parse the next element of the sequence */
        if (parse_element(parser, element))
        {
            /* If succesful, try to parse the remainder of the sequence (and thereafter the remainder of the rule) */
            if (parse_seq(parser, element))
                return TRUE;
        }
    }

    text_buffer_set_pos(parser->text_buffer, &sp);

    if (!element->avoid)
    {
        if (parse_rule(parser, element->next))
            return TRUE;
    }

    return FALSE;
}
```

## Parse an element of a rule

[The `parse_element` function](simple_parser.md#parse-an-element-of-a-rule) is only
adapted with respect to the usage of the parser structure.
```c
bool parse_element(parser_p parser, element_p element)
{
    switch (element->kind)
    {
        case rk_end:
            return text_buffer_end(parser->text_buffer);

        case rk_char:
            if (*parser->text_buffer->info == element->info.ch)
            {
                text_buffer_next(parser->text_buffer);
                return TRUE;
            }
            break;

        case rk_charset:
            if (char_set_contains(element->info.char_set, *parser->text_buffer->info))
            {
                text_buffer_next(parser->text_buffer);
                return TRUE;
            }
            break;

        case rk_nt:
            return parse_nt(parser, element->info.non_terminal);

        case rk_grouping:
            for (rules_p rule = element->info.rules; rule != NULL; rule = rule->next )
            {
                if (parse_rule(parser, rule->elements))
                    return TRUE;
            }
            break;
    }

    return FALSE;
}
```

