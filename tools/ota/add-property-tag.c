/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Append a tag to a property value in a .prop file if it isn't already there.
 * Normally used to modify build properties to record incremental updates.
 */

// Return nonzero if the tag should be added to this line.
int should_tag(const char *line, const char *propname) {
    const char *prop = strstr(line, propname);
    if (prop == NULL) return 0;

    // Make sure this is actually the property name (not an accidental hit)
    const char *ptr;
    for (ptr = line; ptr < prop && isspace(*ptr); ++ptr) ;
    if (ptr != prop) return 0;  // Must be at the beginning of the line

    for (ptr += strlen(propname); *ptr != '\0' && isspace(*ptr); ++ptr) ;
    return (*ptr == '=');  // Must be followed by a '='
}

// Remove existing tags from the line, return the following number (if any)
int remove_tag(char *line, const char *tag) {
    char *pos = strstr(line, tag);
    if (pos == NULL) return 0;

    char *end;
    int num = strtoul(pos + strlen(tag), &end, 10);
    strcpy(pos, end);
    return num;
}

// Write line to output with the tag added, adding a number (if >0)
void write_tagged(FILE *out, const char *line, const char *tag, int number) {
    const char *end = line + strlen(line);
    while (end > line && isspace(end[-1])) --end;
    if (number > 0) {
        fprintf(out, "%.*s%s%d%s", end - line, line, tag, number, end);
    } else {
        fprintf(out, "%.*s%s%s", end - line, line, tag, end);
    }
}

int main(int argc, char **argv) {
    const char *filename = "/system/build.prop";
    const char *propname = "ro.build.fingerprint";
    const char *tag = NULL;
    int do_remove = 0, do_number = 0;

    int opt;
    while ((opt = getopt(argc, argv, "f:p:rn")) != -1) {
        switch (opt) {
        case 'f': filename = optarg; break;
        case 'p': propname = optarg; break;
        case 'r': do_remove = 1; break;
        case 'n': do_number = 1; break;
        case '?': return 2;
        }
    }

    if (argc != optind + 1) {
        fprintf(stderr,
            "usage: add-property-tag [flags] tag-to-add\n"
            "flags: -f /dir/file.prop (default /system/build.prop)\n"
            "       -p prop.name (default ro.build.fingerprint)\n"
            "       -r (if set, remove the tag rather than adding it)\n"
            "       -n (if set, add and increment a number after the tag)\n");
        return 2;
    }

    tag = argv[optind];
    FILE *input = fopen(filename, "r");
    if (input == NULL) {
        fprintf(stderr, "can't read %s: %s\n", filename, strerror(errno));
        return 1;
    }

    char tmpname[PATH_MAX];
    snprintf(tmpname, sizeof(tmpname), "%s.tmp", filename);
    FILE *output = fopen(tmpname, "w");
    if (output == NULL) {
        fprintf(stderr, "can't write %s: %s\n", tmpname, strerror(errno));
        return 1;
    }

    int found = 0;
    char line[4096];
    while (fgets(line, sizeof(line), input)) {
        if (!should_tag(line, propname)) {
            fputs(line, output);  // Pass through unmodified
        } else {
            found = 1;
            int number = remove_tag(line, tag);
            if (do_remove) {
                fputs(line, output);  // Remove the tag but don't re-add it
            } else {
                write_tagged(output, line, tag, number + do_number);
            }
        }
    }

    fclose(input);
    fclose(output);

    if (!found) {
        fprintf(stderr, "property %s not found in %s\n", propname, filename);
        remove(tmpname);
        return 1;
    }

    if (rename(tmpname, filename)) {
        fprintf(stderr, "can't rename %s to %s: %s\n",
            tmpname, filename, strerror(errno));
        remove(tmpname);
        return 1;
    }

    return 0;
}
