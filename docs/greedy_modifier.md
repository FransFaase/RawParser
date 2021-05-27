# Greedy modifier

The parsing of a sequence in [the simple parser](simple_parser.md#parse-a-sequence)
now back-tracks over the sequence. This is needed when the element
following the sequence is similar to the element in the sequence.
An example of this in the C-grammar are the variadic functions that
take a variable number of arguments, something that is indicated
by following the normal list of parameters with a command and three dots
(possibly seperated by white space).

But in most cases there is no need to back-track over the
elements of a sequence, because the element following the sequence
does not overlap with the element of the sequence. 

The same is true for an optional element. If the parsing of the
remainder of the rule fails when the optional element is parsed,
the simple parser tries to parse the remainder of the rule skipping
the optional element. Again, in most cases the optional element
will not be the same as the element following the optional element
does not overlap.

The idea is to introduce a greedy modifier that prevents back-tracking
for optional and/or sequential elements. (There is probably no reason to have
two modifiers, one for optional and one for sequential, because either
the element is same as the following element or not the same.)
Note that the avoid modifier does not work well in combination with the
the greedy modifier. It will be ignored in combination with the greedy
modifier.

the type for an element in a production rule in the following manner:
```c
struct element
{
    ...
    bool greedy;       /* Whether the element is a greedy sequence and/or option */
};

void element_init(element_p element, enum element_kind_t kind)
{
    ...
    element->greedy = FALSE;
}
```

## Definition of the grammar

Because the greedy version is more common, it is probably a good idea to make
the default and change some of [the previous definitions](grammar.md#defines-for-defining-a-grammar).

```c
#define GREEDY element->greedy = TRUE;
#define SEQ element->sequence = TRUE; GREEDY
#define OPT element->optional = TRUE; GREEDY
#define BT_SEQ element->sequence = TRUE;
#define BT_OPT element->optional = TRUE;
```

## Parse a rule

[The `parse_rule` function](caching_parser.md#parse-a-rule) is adapted for the
greedy modifier by adding a loop before the non-greedy (back-tracking) implementation.
Note that adding a greedy modifier to elements that do not have an optional
and/or sequential modifier has the effect to reduce the number of recursive
function calls and thus can be useful.

```c
bool parse_rule(parser_p parser, element_p element)
{
```
*  Repeat parsing an element as long it has the greedy modifier
```c
    for (; element != NULL && element->greedy; element = element->next)
    {
        if (parse_element(parser, element))
        {
```
*  The first element was parsed
```c
            if (element->sequence)
            {
```
*  If the element has the sequence modifier, continue trying to parse it,
   until it fails. Then continue with the next element.
```c
                while (parse_element(parser, element)
                {
                }
            }
            else
            {
```
*  The element is not a sequence. Contiue with the next element.
```c
            }
        }
        else if (element->optional)
        {
```
*  The element was not parsed, but because it has the optional modifier,
   continue with the next element.
```c
        }
        else
```
*  
```c
            return FALSE;
    }

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

