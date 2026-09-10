# Contributing to Documentation

## Setting Up

It's recommended to use a markdown editor that can use [`markdownlint`](https://github.com/DavidAnson/markdownlint). [Visual Studio Code](https://code.visualstudio.com) does a excellent job at this with the [markdownlint extension](https://marketplace.visualstudio.com/items?itemName=DavidAnson.vscode-markdownlint). Another great option is using [Obsidian](https://obsidian.md/download) and [installing the `markdownlint` extension](https://community.obsidian.md/plugins/markdownlint).

## Markdown Standards

We follow the rules laid out by [`markdownlint`](https://github.com/DavidAnson/markdownlint#rules--aliases) with the following exceptions:

- `MD035 no-bare-urls` can be ignored
- `MD041 first-line-heading/first-line-h1` may be ignored if first line heading is a back button, or an image. It is disabled project wide due to the common nature of back buttons.
- `MD-059 descriptive-link-text` can be ignored

View the [configuration file](../.markdownlint.json) to see every single rule ignored.

## Our Standards

- Every single document should contain a back button going back to what linked to it. The back button is a `h2` header hyperlink that uses the :rewind: emoji along with where it goes back to. As an example:

```md
## [:rewind: Modding](lua/modding.md)
```
