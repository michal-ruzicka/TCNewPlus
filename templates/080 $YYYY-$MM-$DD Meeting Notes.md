# Meeting Notes

**Date:** $YYYY-$MM-$DD $hh:$mm

**Attendees:**
1. %USERNAME%


## Agenda

1.


## Notes

### Agenda Item 1

-


## Action Items

- [ ]



___
# Markdown cheat-sheet

A quick reference for the most common Markdown syntax. The left side is what
you type; render the file (or any Markdown viewer) to see the result.


## Headings

```markdown
# Heading 1
## Heading 2
### Heading 3
#### Heading 4
##### Heading 5
###### Heading 6
```


## Emphasis

```markdown
*italic* or _italic_
**bold** or __bold__
***bold italic***
~~strikethrough~~
`inline code`
```

*italic* or _italic_
**bold** or __bold__
***bold italic***
~~strikethrough~~
`inline code`


## Paragraphs and line breaks

```markdown
A blank line separates paragraphs.

End a line with two spaces␣␣
to force a line break.
Or use a backslash\
at the end of the line.
```

A blank line separates paragraphs.

End a line with two spaces  
to force a line break.
Or use a backslash\
at the end of the line.


## Lists

```markdown
- Unordered item
- Another item
  - Nested item (indent by two spaces)
    - Deeper level

1. Ordered item
2. Second item
   1. Nested ordered item

- [ ] Task to do
- [x] Task done
```

- Unordered item
- Another item
  - Nested item (indent by two spaces)
    - Deeper level

1. Ordered item
2. Second item
   1. Nested ordered item

- [ ] Task to do
- [x] Task done


## Links and images

```markdown
[Link text](https://example.com)
[Link with title](https://example.com "Hover title")
<https://example.com>            (auto-link)
[Reference link][ref]

[ref]: https://example.com

![Alt text](path/to/image.png)
![Alt text](path/to/image.png "Hover title")
```

[Link text](https://example.com),
[Link with title](https://example.com "Hover title"),
<https://example.com> (auto-link),
[Reference link][ref]

[ref]: https://example.com


## Blockquotes

```markdown
> A quote.
>
> > Nested quote.
```

> A quote.
>
> > Nested quote.


## Code

````markdown
Inline `code` with backticks.

```
Plain fenced code block.
```

```python
# Fenced block with a language for syntax highlighting
def hello():
    print("Hello")
```

    Or indent a block by four spaces.
````

Inline `code` with backticks.

```
Plain fenced code block.
```

```python
# Fenced block with a language for syntax highlighting
def hello():
    print("Hello")
```

    Or indent a block by four spaces.


## Horizontal rule

```markdown
---
***
___
```

---
***
___


## Tables

```markdown
| Left | Center | Right |
|:-----|:------:|------:|
| a    |   b    |     c |
| d    |   e    |     f |
```

| Left | Center | Right |
|:-----|:------:|------:|
| a    |   b    |     c |
| d    |   e    |     f |


## Escaping

```markdown
Use a backslash to show a literal Markdown character:
\* \_ \` \# \\ \[ \]
```

Use a backslash to show a literal Markdown character:
\* \_ \` \# \\ \[ \]


## Extras (supported by many renderers)

```markdown
Footnote reference.[^1]

[^1]: The footnote text.

Term
: Definition (definition list)

| Emoji | :rocket: :+1: (GitHub-style shortcodes) |
```

Footnote reference.[^1]

[^1]: The footnote text.

Term
: Definition (definition list)

| Emoji | :rocket: :+1: (GitHub-style shortcodes) |


---

*This cheat-sheet covers CommonMark plus common GFM (GitHub Flavored
Markdown) extensions. Renderer support for the "Extras" section varies.*
