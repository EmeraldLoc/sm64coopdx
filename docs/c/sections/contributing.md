## [:rewind: C Reference](../c.md)

# Contributing

## Code Format

This is the coding format and style we use:

- Variable names should use `camelCase`
- Function names should use `snake_case`
- Struct names should use `PascalCase`
- Enum names should use `PascalCase`, with the constants inside the enum being `SCREAMING_SNAKE_CASE`
- Macros are `SCREAMING_SNAKE_CASE`
- Filenames should use `snake_case`
- Global variables, or variables that can be accessed in any file, should be prefixed with a `g` (i.e. `gGlobalTimer`)
- Static variables should be prefixed with an `s` (i.e. `sOverrideCameraCollision`)
- For creating a pointer, `Type *value;` is the proper convention, not `Type* value;`
- For casting to a pointer, use `(type *)value`, not `(type*)value`
- In a header file, use `#pragma once` rather than `#ifndef`/`#define` guard blocks
- Function arguments should always be specified. If empty, use `void`. As an example, use `void allocate_item(void)` instead of `void allocate_item()`
- The brace style is 1TBS (One True Brace Style)
- Indentation is 4 spaces

## Creating a Pull Request

For all pull requests, target the `dev` branch. Your title should be short and clear, and your description should describe the changes made, and most importantly your reasoning behind any changes made. If possible, document your changes wherever appropriate, either the C documentation or the Lua documentation.

## Reviewing Pull Requests

Any help reviewing pull requests is greatly appreciated. Reviewing can range from pointing out very minor issues to constructively criticizing an entire pull request. Whatever the case, present your issues with clarity and respect.

More reviews means a pull request can be merged quicker, so the more the merrier.
