#include <stdio.h>
#include <stdlib.h>

#include "bread.h"

int
main(void)
{
    for (;;) {
        char *line = bread_line("> ");
        printf("You entered: %s\n", line);
        free(line);
    }
}
