# Caching parser

An approach to make the execution of [the simple parser](simple_parser.md)
more efficient, it to implement caching. One this page the implementation
of a caching mechanism for parsed non-terminals is given. Because there
are many possible caching algorithms, a function pointer to a cache
function together with a void pointer to a cache data structure is used.

One approach to improve the performance of a back-tracking recursive-descent
parser (such as [the simple parser](simple_parser.md)) is to use a cache
were intermediate results are stored. One this page the implementation
of a caching mechanism for parsed non-terminals is given. Because there
are various caching strategies an abstract interface for caching is provided.
This abstract interface consists of two function pointer and a void pointer
to a caching data. If the first function pointer is not null, the function is
called at the start of parsing a non-terminal to query the cache. If a cache
item is returned, it will be stored in a local variable and whenever it is
determined that the non-terminal could be parsed or not, the function pointed
by the second function pointer is called if it is not null, otherwise it is
assumed that the returned cache item is still valid and can be updated
accordingly.

There are three possible states for a cache item: unknown, fail, or success:
```c
enum success_t { s_unknown, s_fail, s_success } ;
```

A cache item consists of a state and when the state is success, also the
position from which parsing should continue:
```c
typedef struct cache_item cache_item_t, *cache_item_p;
struct cache_item
{
    enum success_t success;  /* Could said non-terminal be parsed from position */
    text_pos_t next_pos;     /* and if so, from which position should parsing continue */
};
```

The abstract interface for the cache, as stated before, consists of two
function pointers and a void pointer, as defined in the following struct
(together with an initialization function):
```c
typedef struct cache cache_t, *cache_p;
struct cache
{
    cache_item_p (*get_cache_item)(cache_p cache, text_pos_p start_pos, non_terminal_p nt);
    void (*set_result)(cache_item_p cache_item, cache_p cache, text_pos_p start_pos, enum success_t success, text_pos_p next_pos); 
    void *cache_data;
};

void cache_init(cache_p cache)
{
    cache->get_cache_item = 0;
    cache->set_result = 0;
    cache->cache_data = NULL;
}
```

## Parser struct

In the simple parser, a pointer to the text buffer was passed to the
various parsing functions. Instead of passing another pointer to the
abstract cache with each of the parsing functions, it is better to
combine them into a single struct representing the parser with the
following declaration (together with an initialization function):
```c
typedef struct parser parser_t, *parser_p;
struct parser
{
    text_buffer_p text_buffer;
    cache_t cache;
};

void parser_init(parser_p parser, text_buffer_p text_buffer)
{
    parser->text_buffer = text_buffer;
    cache_init(&parser->cache);
}
```
Now in all parsing functions the first text buffer parameter is
replaced by `parser_p parser`.

## Parse a non-terminal

Below the adapted function definition for [parsing a non-terminal](simple_parser.md#parse-a-non-terminal)
is given.
```c
bool parse_nt(parser_p parser, non_terminal_p non_term)
{
```
* Save the current location. If the `get_cache_item` function is
  defined, call it.
```c
    text_pos_t start_pos = parser->text_buffer->pos;

    cache_item_p cache_item = NULL;
    if (parser->cache.get_cache_item != 0)
    {
        cache_item = parser->cache.get_cache_item(&parser->cache, &parser->text_buffer->pos, non_term);
        if (cache_item != NULL)
        {
```
* A cache item was returned. If it indicates success, it means that
  a succesful attempt was made to parse the non-terminal at this
  location. Set the text buffer to the next location is stored in
  the cache item.
```c
            if (cache_item->success == s_success)
            {
                text_buffer_set_pos(parser->text_buffer, &cache_item->next_pos);
                return TRUE;
            }
```
* If it was a failure, then simply return FALSE.
```c
            else if (cache_item->success == s_fail)
            {
                return FALSE;
            }
```
* Otherwise, the cache returns that it does not know if the
  non-terminal could be parsed starting from the current location.
  The success state is set to failure to deal with indirect
  left-recursion.
```c
            cache_item->success = s_fail;
        }
    }

    bool parsed_a_rule = FALSE;
    for (rules_p rule = non_term->normal; rule != NULL; rule = rule->next)
    {
        if (parse_rule(parser, rule->elements))
        {
            parsed_a_rule = TRUE;
            break;
        }
    }

    if (!parsed_a_rule)
    {
```
* An attempt to parse one of the normal production rules of the non-terminal
  failed. Now if the `set_result` function pointer is not null, the function
  is called. Otherwise the cache item is updated.
```c
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
            if (parse_rule(parser, rule->elements))
            {
                parsed_a_rule = TRUE;
                break;
            }
        }
    }

```
* One of the normal production rules has been parsed and possibly zero or more
  left recursive production rules. Now if the `set_result` function pointer is not
  null, the function is called. Otherwise the cache item is updated.
```c
    if (cache_item != NULL)
    {
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

## Parse a rule

[The `parse_rule` function](simple_parser.md#parse-a-rule) is only adapted
with respect to the usage of the parser structure.
```c
bool parse_rule(parser_p parser, element_p element)
{
    if (element == NULL)
        return TRUE;

    if (element->optional && element->avoid)
    {
        if (parse_rule(parser, element->next))
            return TRUE;
    }

    text_pos_t sp = parser->text_buffer->pos;

    if (parse_element(parser, element))
    {
        if (element->sequence)
        {
            if (parse_seq(parser, element))
                return TRUE;
        }
        else
        {
            if (parse_rule(parser, element->next))
                return TRUE;
        }
    }

    text_buffer_set_pos(parser->text_buffer, &sp);

    if (element->optional && !element->avoid)
    {
        if (parse_rule(parser, element->next))
            return TRUE;
    }

    return FALSE;
}
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

# Caching methods

There is a range of [different caching methods](https://en.wikipedia.org/wiki/Cache_(computing)),
which range from only caching the latest result to a function
call to storing all the results (basically [memoization](https://en.wikipedia.org/wiki/Memoization))
and everything in between.

## A memoization cache

Below an implementation of a memoization cache, which simple stores
the result of all calls to `parse_nt`.

```c
typedef struct solution *solution_p;
struct solution
{
    cache_item_t cache_item;
    const char *nt;
    solution_p next;
};
typedef struct
{
    solution_p *sols;        /* Array of solutions at locations */
    size_t len;              /* Length of array (equal to length of input) */
} solutions_t, *solutions_p;


void solutions_init(solutions_p solutions, text_buffer_p text_buffer)
{
    solutions->len = text_buffer->buffer_len;
    solutions->sols = MALLOC_N(solutions->len+1, solution_p);
    size_t i;
    for (i = 0; i < solutions->len+1; i++)
        solutions->sols[i] = NULL;
}

void solutions_free(solutions_p solutions)
{
    size_t i;
    for (i = 0; i < solutions->len+1; i++)
    {    solution_p sol = solutions->sols[i];

        while (sol != NULL)
        {
            solution_p next_sol = sol->next;
            FREE(sol);
            sol = next_sol;
        }
      }
    FREE(solutions->sols);
}

cache_item_p solutions_find(cache_p cache, text_pos_p pos, non_terminal_p nt)
{
    solutions_p solutions = (solutions_p)cache->cache_data;
    solution_p sol;

    size_t p = pos->pos;
    if (p > solutions->len)
        p = solutions->len;

    for (sol = solutions->sols[p]; sol != NULL; sol = sol->next)
        if (sol->nt == nt->name)
             return &sol->cache_item;

    sol = MALLOC(struct solution);
    sol->next = solutions->sols[p];
    sol->nt = nt->name;
    sol->cache_item.success = s_unknown;
    solutions->sols[p] = sol;
    return &sol->cache_item;
}
```

# Continue...

The caching parser can be tested with [the caching parser test](simple_parser_test.md).
Continue reading the development of the parser on [the greedy modifier](greedy_modifier.md).
