# Typewriter Effect with Rich Text + *Correct* Text Wrapping

I've spent way too long getting this right. This is meant as a base class for a UMG dialogue box that uses a typewriter effect to
display text. Meant for use with rich text - basic text boxes would use a simpler method involving Slate's font measurement system. If
anyone is interested, I can post that version as well.

This is not meant to be used as-is as it uses some types specific to my project, but it should be simple to swap them out for whatever
you want.

![QY50iAnFHU](https://user-images.githubusercontent.com/472625/120946044-fd0e7a80-c6ef-11eb-91dc-e9ce39d5dcbf.gif)


## Known Issues/Limitations

* Most if not all rich text features are supported, including basic formatting and custom decorators (and images!).
* Changing font-size (or using a decorator that's taller than the existing text) anywhere except at the beginning of a line will result in
  the entire line "jumping" down slightly to accomodate the new text size. I don't have a solution to this yet and don't see myself using
  different font sizes much, so it isn't something I'm likely to get to any time soon.
* There may be some hidden i18n issues due to all the conversions between `FString`/`FText` and string indexing.
* This has been tested with UE 5.2.0, though it should work fine with earlier/later versions.
* The current implementation was quickly thrown together (see: hacky) and somewhat unoptimized. Some data is duplicated more than it needs
  to be, and "segment" calculation is a bit more complex than I'd like.

## What's wrong with the "naive" approach?

There are two issues - one that affects any sort of typewriter effect with text wrapping, and one that specifically
effects rich text (at least as it is implemented in Slate).

### 1. Auto Text Wrapping

The problem with just using auto text wrapping is that words towards the end of a line may "jump" to the next line as they are typed out.
For example, take this "dialogue box":

```
+----------------------+
| Hello, world! This   |
| dialogue box is cool.|
+----------------------+
```

While the word "dialogue" is being written, it can appear like this:

```
+----------------------+
| Hello, world! This di|
|                      |
+----------------------+
```

but as soon as the next letter is written, "dialogue" will jump to the next line due to being too long:

```
+----------------------+
| Hello, world! This   |
| dia                  |
+----------------------+
```

This is jarring, especially if the reader is reading at the same speed that the text is being typed out. The general fix is to precalculate where
the text needs to wrap, and then follow those line breaks instead of calculating new ones every time the effect adds a new letter.

With normal text blocks this is (mostly) simple: use the slate font measurement service to figure out how long text is and programmatically insert newlines
until it fits within the bounds of your dialogue box. Rich text requires some more trickery due to supporting arbitrary numbers of fonts, sizes,
weights, and even images.

### 2. Rich Text Tags

Rich text adds in another level of complications - you can't just iterate over the text to display it, as you'll end up with partial markup tags.
Take this string:

```
Hello, <Red>World</>!
```

If you were to use a "naive" typewriter effect, you'd end up sending partial `<Red>` tags at various points during the effect. A couple of examples:

* `Hello, <Red` (what's `<Red` doing there?)
* `Hello, <Red>World` (missing the closing tag)

Unreal's rich text makes this even worse by outright not rendering sections of "bad" markup, meaning that `world` will suddenly pop into existence when
the closing tag is written rather than smoothly appearing letter-by-letter.

## The Solution

My current solution is a bit hacky and I haven't taken the time to optimize it (yet - there are some obvious areas that I may go back and fix). The
basic idea is to hook into the rich text block's creation of its `FSlateTextLayout` and `FRichTextLayoutMarshaller` which, together, is what figures
out how to parse and draw rich text on the screen.

Once we have pointers to both of those, we can briefly ask them to do some layout work for us on the final string we want to print, grab the different
"runs" that make up the layout, then return control back to the rich text widget. We can then use the information about how the layout is going to work
to insert our own newlines and move the markup tags around.

This *would* be much simpler if there were APIs to pass pre-parsed text data into a rich text widget, but no such APIs exist without maintaining our own
separate copy of it (which is something I might do in the future, but this was much quicker to implement).

### Alternative Method (that would be better but doesn't work)

The main alternative method that I tried was implementing `IBreakIterator` and passing it in to the text layout. This (theoretically) lets us tell the
layout where it is allowed to insert a line break. We can use the normal line break iterator to pre-process the entire text and decide where line breaks should
go. We store that line break data and then instead of processing the incoming text from the layout as we're running the effect, we use the cached line
break information from earlier.

This would work if it wasn't for one sort-of-bug with text layout - if you don't tell the text layout it can place a line break at the final character
of a string then it never ends up drawing that final section.
