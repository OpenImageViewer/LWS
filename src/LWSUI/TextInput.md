# TextBox input policies

TextBox stays unrestricted by default. SetInputFilter installs an optional callback that receives the proposed whole value and TextEditOrigin. Return an empty expected to accept it or unexpected with a message to reject the whole edit. Keep the callback free of control mutations.

Typing, paste, cut, deletion, and selection replacement use the same prepare and apply path. A rejection preserves text, selection, the draft, and semantic validation. It emits OnInputRejected instead of OnEdit and shows a red underline. InputError contains this feedback. Rejection feedback does not prevent committing an unchanged valid value. Accepted edits, cancellation, successful commit, assignment, and policy changes clear it. Policy changes also invalidate pending clipboard requests.

SetText is silent and bypasses the filter. It resets the draft and semantic validation. A masked assignment must fit the current mask, otherwise it leaves the control unchanged.

## Fixed masks

SetInputMask returns an expected result. Installing a mask is transactional and requires a single-line TextBox and a compatible current value. An empty pattern removes the mask. Patterns use printable ASCII, with 0 for a digit, A for an ASCII letter, and X for an ASCII letter or digit. Other characters are literals. A backslash escapes a token or literal. Underscore is reserved for empty slots.

The editing buffer contains the full display, including persistent underscore placeholders and automatic literals. Text and edit events omit placeholders. UnmaskedText returns only entered slot characters. Empty masks report an empty Text even though their display contains literals. MaskComplete reports whether every slot is filled. A nonempty incomplete value cannot commit. Empty values can commit unless a semantic validator rejects them.

Typing overwrites slots. Backspace and Delete clear slots without shifting later fields. Paste accepts raw slot characters or matching separators. Invalid characters, incorrect separators, and excess input reject the entire operation. Copy excludes placeholders. Masked values with cleared interior slots retain their display positions while editing, but Text and UnmaskedText are compact value projections rather than a lossless draft serialization.

Use SetValidation in OnEdit for calendar validity, required fields, numeric bounds, and other semantic rules. Syntax filters allow partial drafts and do not replace commit validation.

```cpp
TextBox phone;
auto installed = phone.SetInputMask("(000) 000-0000");
TextBox amount;
amount.SetInputFilter(InputFilters::Number());
```

InputFilters::Number supplies integer or decimal draft syntax, with optional signed values and exponents. InputFilters::Hexadecimal supplies plain hex or color syntax. NumericEdit installs Number automatically, which also covers editable Slider values and numeric GridView editors. ColorPicker and the color panel share the color filter. Color panel channels use integer or fixed-decimal filters matching their existing parsers. Search and file paths remain unrestricted.

## Hexadecimal forms

The general hexadecimal filter accepts HexadecimalFormFlags, an LLUtils::BitFlags<HexadecimalForm> value. Combine Bare, HashPrefix, and ZeroXPrefix to choose accepted forms. ZeroXPrefix accepts both 0x and 0X. The filter preserves entered case. None is an uninitialized configuration sentinel, rather than an input form. Explicit None or unknown bits throw std::invalid_argument when constructing the filter. Initialize stored flag values explicitly.

```cpp
HexadecimalFormFlags forms{HexadecimalForm::HashPrefix | HexadecimalForm::ZeroXPrefix};
field.SetInputFilter(InputFilters::Hexadecimal(forms));
```

This configuration accepts #FF and 0xFF as complete values. Draft filtering also permits empty text, prefix-only text, and an initial 0 while typing 0x. Use semantic validation to require a complete value. The no-argument overload still accepts bare digits. The existing boolean color overload retains its required # prefix and eight-digit maximum.
