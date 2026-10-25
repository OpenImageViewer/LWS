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

InputFilters::Number supplies integer or decimal draft syntax, with optional signed values and exponents. InputFilters::Hexadecimal supplies plain hex or color syntax.

## Hexadecimal forms

The general hexadecimal filter accepts HexadecimalFormFlags, an LLUtils::BitFlags<HexadecimalForm> value. Combine Bare, HashPrefix, and ZeroXPrefix to choose accepted forms. ZeroXPrefix accepts both 0x and 0X. The filter preserves entered case. None is an uninitialized configuration sentinel, rather than an input form. Explicit None or unknown bits throw std::invalid_argument when constructing the filter. Initialize stored flag values explicitly.

```cpp
HexadecimalFormFlags forms{HexadecimalForm::HashPrefix | HexadecimalForm::ZeroXPrefix};
field.SetInputFilter(InputFilters::Hexadecimal(forms));
```

This configuration accepts #FF and 0xFF as complete values. Draft filtering also permits empty text, prefix-only text, and an initial 0 while typing 0x. Use semantic validation to require a complete value. The no-argument overload still accepts bare digits. The existing boolean color overload retains its required # prefix and eight-digit maximum.

## Dynamic field masks

`TextBox::SetInputMask(const DynamicMaskDefinition&)` installs a dynamic mask.
Each `DynamicMaskProfile` specifies a prefix, numeric base from 2 to 36, field
count, minimum and maximum digits, inclusive numeric bounds, and one ASCII
separator. Definitions must contain unique prefixes and valid field bounds.
The overload returns an error without changing the previous mask or text when
the definition or existing value is incompatible. Static mask calls retain
their existing behavior, including date masks.

Dynamic masks display entered digits and generated separators without padding
or placeholders. Completing a field inserts the next separator and advances
the caret. Typing the separator can finish a nonempty field early once its
minimum width and bounds are satisfied. Insertions stay within their field.
Backspace and Delete preserve field boundaries. Whole-field replacement can
select another profile. Profiles can opt into `inferFromDigits` to select their
prefix when undecided input contains digits outside the unprefixed profile's
base.
Profile selection is committed atomically with text and restored on cancellation.
Prefix detection retains provisional input such as
`0` while typing `0x`. Incomplete drafts are editable but cannot commit.

Mask operations use ordinary TextBox display offsets and publish the proposed
text and caret together. Rejected edits preserve text, selection, and profile.
`UnmaskedText()` removes the prefix and field separators. An optional
`normalizePaste` callback receives pasted text and an existing prefix, and
transforms whole-field or prefix-only paste before mask validation.
Normalization failures leave the draft unchanged.

## Invalid edits when leaving a control

`UIHost` defaults to `InvalidEditPolicy::KeepInvalid`. Invalid drafts can
lose focus while preserving their text and edit-start snapshot. Explicit
`Finish(EditPhase::Commit)` remains strict. Applications validate drafts before
saving their form. LWSSettings aggregates field errors in a modal message box.

`BlockOnLeave` refuses departure until the draft is corrected or cancelled.
`DiscardOnLeave` restores the edit-start value and continues the interaction.
Set the host default with `SetInvalidEditPolicy`. Set an optional override on
any control or composite with the same method. The nearest ancestor override
wins. `std::nullopt` restores inheritance. Changing policy leaves drafts intact.

Filter rejection preserves text and selection. The focused textbox displays
its rejection explanation in an overlay beside the field, including inside
popups. Accepted edits and finishing the interaction clear that feedback.
Rejected attempts do not make an otherwise valid value fail form validation.
Invalid input has an underline at least 3 logical pixels thick.

## Clipboard aliases

TextBox supports Ctrl+Insert for Copy and Shift+Insert for Paste in addition
to Ctrl+C and Ctrl+V. These use the existing clipboard operations and pending
paste safeguards. Read-only fields allow Copy and block Paste. GridView also
supports Ctrl+Insert for selection copying and does not support Paste.

## Validation discovery

`UIHost::CollectValidationIssues(root)` is a non-mutating query returning control
handles, messages, and effective departure policies. It supports
detached subtrees and includes a hosted subtree's logically owned popup.
Popup fields inherit the departure policy from their owner unless overridden locally.
Applications add their own field names and submission or dismissal decisions.
The same textbox validity query governs explicit commit and form preflight,
including the existing optional-empty rule for static masks.
