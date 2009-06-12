Update scripts (from donut onwards) are written in a new little
scripting language ("edify") that is superficially somewhat similar to
the old one ("amend").  This is a brief overview of the new language.

- The entire script is a single expression.

- All expressions are string-valued.

- String literals appear in double quotes.  \n, \t, \", and \\ are
  understood, as are hexadecimal escapes like \x4a.

- String literals consisting of only letters, numbers, colons,
  underscores, slashes, and periods don't need to be in double quotes.

- The following words are reserved:

       if    then    else   endif

  They have special meaning when unquoted.  (In quotes, they are just
  string literals.)

- When used as a boolean, the empty string is "false" and all other
  strings are "true".

- All functions are actually macros (in the Lisp sense); the body of
  the function can control which (if any) of the arguments are
  evaluated.  This means that functions can act as control
  structures.

- Operators (like "&&" and "||") are just syntactic sugar for builtin
  functions, so they can act as control structures as well.

- ";" is a binary operator; evaluating it just means to first evaluate
  the left side, then the right.  It can also appear after any
  expression.

- Comments start with "#" and run to the end of the line.



Some examples:

- There's no distinction between quoted and unquoted strings; the
  quotes are only needed if you want characters like whitespace to
  appear in the string.  The following expressions all evaluate to the
  same string.

     "a b"
     a + " " + b
     "a" + " " + "b"
     "a\x20b"
     a + "\x20b"
     concat(a, " ", "b")
     "concat"(a, " ", "b")

  As shown in the last example, function names are just strings,
  too.  They must be string *literals*, however.  This is not legal:

     ("con" + "cat")(a, " ", b)         # syntax error!


- The ifelse() builtin takes three arguments:  it evaluates exactly
  one of the second and third, depending on whether the first one is
  true.  There is also some syntactic sugar to make expressions that
  look like if/else statements:

     # these are all equivalent
     ifelse(something(), "yes", "no")
     if something() then yes else no endif
     if something() then "yes" else "no" endif

  The else part is optional.

     if something() then "yes" endif    # if something() is false,
                                        # evaluates to false

     ifelse(condition(), "", abort())   # abort() only called if
                                        # condition() is false

  The last example is equivalent to:

     assert(condition())


- The && and || operators can be used similarly; they evaluate their
  second argument only if it's needed to determine the truth of the
  expression.  Their value is the value of the last-evaluated
  argument:

     file_exists("/data/system/bad") && delete("/data/system/bad")

     file_exists("/data/system/missing") || create("/data/system/missing")

     get_it() || "xxx"     # returns value of get_it() if that value is
                           # true, otherwise returns "xxx"


- The purpose of ";" is to simulate imperative statements, of course,
  but the operator can be used anywhere.  Its value is the value of
  its right side:

     concat(a;b;c, d, e;f)     # evaluates to "cdf"

  A more useful example might be something like:

     ifelse(condition(),
            (first_step(); second_step();),   # second ; is optional
            alternative_procedure())
