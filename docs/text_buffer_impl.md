# Implementation of text buffer

Below we give an implementation for the text buffer as it is defined at the
start of [a simple parser](simple_parser.md).

First the function to assign a zero terminated string to the text buffer
and initialize it. It sets the position at the start of the text.
```c
void text_buffer_assign_string(text_buffer_p text_buffer, const char* text)
{
	text_buffer->tab_size = 4;
	text_buffer->buffer_len = strlen(text);
	text_buffer->buffer = text;
	text_buffer->info = text_buffer->buffer;
	text_buffer->pos.pos = 0;
	text_buffer->pos.cur_line = 1;
	text_buffer->pos.cur_column = 1;
}
```

The function `text_buffer_next` advances the position one character as long
as it is not at the end of the buffer. It also updates the current line and
column numbers.
```c
void text_buffer_next(text_buffer_p text_buffer)
{
	if (text_buffer->pos.pos < text_buffer->buffer_len)
	{
	  switch(*text_buffer->info)
	  {   case '\t':
			  text_buffer->pos.cur_column += text_buffer->tab_size - (text_buffer->pos.cur_column - 1) % text_buffer->tab_size;
			  break;
		  case '\n':
			  text_buffer->pos.cur_line++;
			  text_buffer->pos.cur_column = 1;
			  break;
		  default:
			  text_buffer->pos.cur_column++;
			  break;
	  }
	  text_buffer->pos.pos++;
	  text_buffer->info++;
	}
}
```

A simple function to test if the end of the text buffer has been reached.
```c
bool text_buffer_end(text_buffer_p text_buffer) {
 	return text_buffer->pos.pos >= text_buffer->buffer_len;
}
```

By assigning the value of the `pos` member to a variable of type `text_pos_t`
a position can be remembered. The following function can be used to restore
that position. Besides assigning the `pos` member (after a sanity check) it
also sets the `info` members correctly.
```c
void text_buffer_set_pos(text_buffer_p text_file, text_pos_p text_pos)
{
	if (text_file->pos.pos == text_pos->pos)
		return;
	text_file->pos = *text_pos;
	text_file->info = text_file->buffer + text_pos->pos;
}

```
