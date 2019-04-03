# bread

bad line reader

## Usage

There's only a single function:

```c
char *bread_line(const char *prompt);
```

Prints `prompt` and reads a line. Returns an (owning) pointer to the line.
Returns `NULL` if there was an error.
