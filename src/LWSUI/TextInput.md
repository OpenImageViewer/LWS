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

InputFilters::Number supplies integer or decimal draft syntax, with optional signed values and exponents. InputFilters::Hexadecimal supplies plain hex or color syntax. NumericEdit installs Number automatically, which also covers editable Slider values and numeric GridView editors. ColorPicker and the color panel use dynamic field masks. Color panel channels use integer or fixed-decimal filters matching their existing parsers. Search and file paths remain unrestricted.

The Controls Gallery in LWSUIDemo contains integer, decimal, hexadecimal, hex color, date, time, phone, and product ID examples with instructions and rejection feedback.

## Hexadecimal forms

The general hexadecimal filter accepts HexadecimalFormFlags, an LLUtils::BitFlags<HexadecimalForm> value. Combine Bare, HashPrefix, and ZeroXPrefix to choose accepted forms. ZeroXPrefix accepts both 0x and 0X. The filter preserves entered case. None is an uninitialized configuration sentinel, rather than an input form. Explicit None or unknown bits throw std::invalid_argument when constructing the filter. Initialize stored flag values explicitly.

```cpp
HexadecimalFormFlags forms{HexadecimalForm::HashPrefix | HexadecimalForm::ZeroXPrefix};
field.SetInputFilter(InputFilters::Hexadecimal(forms));
```

This configuration accepts #FF and 0xFF as complete values. Draft filtering also permits empty text, prefix-only text, and an initial 0 while typing 0x. Use semantic validation to require a complete value. The gallery demonstrates completion validation with the shared internal checker. The no-argument overload still accepts bare digits. The existing boolean color overload retains its required # prefix and eight-digit maximum.

## Hexadecimal grouping and color text

Both Hexadecimal filter overloads accept optional `spacing` and `groupSize`
arguments. Spacing is the number of ASCII spaces between groups, and group size
is the number of hexadecimal digits in each group. Omitted or zero spacing
keeps compact-only syntax. A zero group size is rejected. Existing calls keep
their behavior.

```cpp
field.SetInputFilter(InputFilters::Hexadecimal(forms, 2, 4));
```

This accepts compact text or groups such as `0xAABB  CCDD`. Drafts may contain
incomplete groups and separators so insertion, deletion, and replacement remain
possible. Complete validation through internal::CheckHexadecimal enforces group width and
spacing. The filter does not insert or remove spaces.

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
base. Color input uses this to select `#` on A–F while preserving provisional
digits. A typed comma resolves the ambiguity as decimal. Automatic decimal
advancement alone remains undecided, so typing `123F` produces `#12-3F-`.
Profile selection is committed atomically with text and restored on cancellation.
Prefix detection retains provisional input such as
`0` while typing `0x`. Incomplete drafts are editable but cannot commit.

Mask operations use ordinary TextBox display offsets and publish the proposed
text and caret together. Rejected edits preserve text, selection, and profile.
`UnmaskedText()` removes the prefix and field separators. An optional
`normalizePaste` callback receives pasted text and an existing prefix, and
transforms whole-field or prefix-only paste before mask validation.
Normalization failures leave the draft unchanged.

ColorPicker and its popup install hexadecimal and decimal RGBA profiles.
Hexadecimal uses four two-digit bytes, such as `#FF-F0-D2-FF`, with `#`, `0x`,
or `0X` prefixes. Decimal uses four comma-separated channels from 0 to 255,
such as `255,240,210,255`. Three decimal digits advance automatically, and a
comma can finish a shorter channel. Incomplete values preview entered digits
at their current numeric value. Missing RGB channels use 0 and missing alpha
uses 255, independently of the previous color. These temporary visuals do not
publish an incomplete value.
Deleting all decimal digits clears the mask. Deleting all hexadecimal digits
leaves its prefix, and deleting that prefix clears the mask. Empty input waits
for either format without placeholders.

Whole-field paste also accepts legacy compact and space-separated hexadecimal,
including bare RGB/RGBA and RGB with implicit opaque alpha. ASCII whitespace
around values, after prefixes, and between hexadecimal bytes is ignored. Pasting
`#  123f56` into a field containing `#`, or `123f56` into an empty field, produces
`#12-3F-56-FF`. Color helpers normalize accepted paste to hyphenated RGBA.
Accepted format and prefix survive picker adjustments and
programmatic `SetValue` calls. Cancellation restores the edit-start color and
presentation. Settings persistence continues to use canonical `#RRGGBBAA`.

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

## Text span providers and channel colors

`TextBox::SetTextSpanProvider` accepts a callback returning `std::vector<TextSpan>`
for the current display string. Ranges use UTF-8 byte offsets and backgrounds
can be transparent. Empty, out-of-bounds, and partial-code-point spans are
ignored. Providers run during rendering and must not mutate controls. An empty
callback restores ordinary rendering. Search highlights override provider
spans, and selection colors override both.

Color fields use red `#FF5555FF`, green `#00FF00FF`, blue `#3399FFFF`, and
white `#FFFFFFFF` for channel digits. The inline value, popup value, Original
value, and separate R/G/B/A numeric boxes share these colors and a stable
charcoal background `#161616FF`. Prefixes and separators are white. H/S/L boxes retain normal text
and theme backgrounds. Hexadecimal
digits are displayed in uppercase while an accepted `0x` or `0X` prefix is
preserved. Settings serialization remains unchanged.

`TextBox::SetBackground` accepts an optional background override, used in both
focused and unfocused states. An explicit background is painted for borderless
fields too. Passing `std::nullopt` restores normal theme or borderless behavior.
Dynamic profiles may set `uppercaseDigits` to normalize entered digits without
changing the profile prefix or caret offsets.

Color-control presentation is shared by the inline value, popup value, Original
value, and separate R/G/B/A boxes. The charcoal fill remains unchanged on focus.
`TextBox::SetFocusOutline(true)` adds an inset outline using the theme accent
and focus width, without changing geometry. The outline is opt-in for ordinary
textboxes. Validation underlines paint above it, and selection retains its
existing foreground and background. H/S/L boxes keep their theme presentation.

Color textboxes use semibold weight 600 and a minimum size of 16 logical pixels.
`TextBox::SetFontWeight` accepts an optional weight override, and
`SetMinimumFontSize` clamps the inherited font size. An empty weight or a zero
minimum restores theme inheritance. Font family and larger theme sizes remain
inherited, including after runtime theme changes. Measurement, caret movement,
hit testing, wrapping, and rendering use the same resolved font. H/S/L fields
keep their existing typography.

Pasting into a color textbox starts a new color draft, replacing its current
value regardless of the caret or selection. Partial decimal channels such as
`111,111` and partial hexadecimal such as `123F` remain editable and preview
with missing RGB channels at 0 and missing alpha at 255. They do not commit
until complete. Escape restores the value from before the edit. Dynamic masks
opt into whole-value paste with `pasteReplacesValue`, which defaults to false
for other masks. Invalid paste leaves the draft unchanged.

## RGB completion and byte notation

Undecided combined color input switches to hexadecimal when a numeric field
would exceed decimal 255. Typing or pasting `333` therefore yields `#33-3`.
An explicitly typed decimal comma keeps decimal validation instead.

A combined color is valid when RGB is present. Decimal `12,34,56` commits with
alpha 255. Hex requires two red digits, two green digits, and at least one blue
digit. A single blue or alpha hex digit is the high nibble, with a zero low
nibble. Thus `1234F` is `#1234F0FF`, and `123456A` is `#123456A0`.
Commit normalizes these forms to full RGBA, while typing preserves the draft.
Missing alpha stays opaque until entered. Missing required RGB remains an
uncommitted preview. The mask's optional `isComplete` callback supplies this
color-specific completion rule without changing ordinary masks.

The popup's individual R/G/B/A boxes accept decimal bytes or hexadecimal bytes
such as `FF`, `#FF`, `0xFF`, and `0XFF`. Individual hex bytes use ordinary numeric
notation, so `#F` is 15. They retain accepted hex notation during popup updates.
Values outside a byte remain invalid. H/S/L boxes continue to accept decimal
values only.

## Double-click notation switching

Double-click a combined color field or an individual R/G/B/A field to toggle
hexadecimal and decimal notation. Each field keeps its own format and remembers
its last hex prefix, defaulting to `#`. The read-only Original field changes
only its presentation. H/S/L boxes retain their normal behavior. Popup preview
updates preserve the owner's inline format.

An incomplete combined field converts its temporary preview, using zero for
missing RGB and 255 for missing alpha. Empty or prefix-only byte fields use
zero for R/G/B and 255 for alpha. Editable conversion is a preview edit, with
focus retained and the caret moved to the end. Escape restores the edit-start
value and presentation. The action does not invoke Save or Apply. Selection,
copying, and ordinary dragging remain available.

`TextBox::SetDoubleClickAction` installs an optional boolean callback. Returning
true consumes a left double press, and false retains ordinary pointer handling.
`TextBox::EditText` replaces the editable draft through mask and filter
validation, preserves its edit-start snapshot, and emits a Preview edit.
Read-only textboxes reject EditText. Pointer interaction and conversion invalidate
pending asynchronous paste requests.

Failed focus departure displays the validation reason next to the field,
including an Escape reminder. Editing or cancelling clears this feedback.

## Validation discovery

`UIHost::CollectValidationIssues(root)` is a non-mutating query returning control
handles, messages, and effective departure policies. It supports
detached subtrees and includes a hosted subtree's logically owned popup.
Popup fields inherit the departure policy from their owner unless overridden locally.
Applications add their own field names and submission or dismissal decisions.
The same textbox validity query governs explicit commit and form preflight,
including the existing optional-empty rule for static masks.
