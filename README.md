# RawParser

This repository contains a single C-file, which, by example,
shows how a grammar driven, scannerless parser can be implemented.

Grammar driven means that this is not a compiler that reads
a grammar and either generates code (implementing the parser)
or a set of tables to drive a parsing algorithm. An example of
the latter is yacc/[bison](https://www.gnu.org/software/bison/)
Instead the parsing algorithm directly operates on the grammar
specification, which allows to implement a rich grammar with
optional, sequential and grouping, of grammar elements. This
specification is represented by structs that point to each
other. The construction of the grammar is aided by some clever
defines to increase readability.

Scannerless means that scanner specification is an intergral
part of the grammar specification.

It is based on ideas I got from developing [IParse](https://github.com/FransFaase/IParse).
The first idea was to return to C. (The first version of IParse
were written in C. See: http://www.iwriteiam.nl/MM.html)
The second idea was to make it scannerless, whereas IParse uses
hand-coded scanners. IParse does contain some low-level scanners
and introduced the concept of character sets. A problem is that
IParse has a build-in mechanism for creating abstract syntax trees
(called abstract parse tree in the software) that does not allow
to combine characters into basic values (as the hard-coded scanner
can do). The idea is to abstract from this in the parser by the
use of void pointers to the data being constructed during parsing
and to make use of pointers to functions in the grammar that
perform the construction at various points of the parsing process.

I am also structuring the code and adding a narrative in comments
in order to make it more accessible and explain the various aspects
of parsing a complex data structure from a textual representation.
Examples of usage are given through out the code, which aims at
implementing a complete parser for C like language. 
It seems by attempting this, it has improved the quality of the
code as well. As with every attempt to write software, there are
still many ad hoc decisions that are debatable.

## documentation

I have also started to document the code in a [literary programming](https://en.wikipedia.org/wiki/Literate_programming)
style with fragments of code that are extended in steps as described in
[Literate programming with Markdown](https://www.iwriteiam.nl/D2101.html#13).
See:

*  [Representation of the grammar](docs/grammar.md)
*  [Parser](docs/parser.md)
