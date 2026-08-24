# Generated fixtures

Protocol shapes, written by a generator, committed, and runnable in CI with no
hardware. **Nothing is here yet, and nothing may be added before VRC-2.**

A generated fixture pins a shape, and a shape is something a decoder defines. The
VRChat OSC surface is published, which makes writing these from the document
tempting and is exactly the reason not to: a specification says what a *receiver*
must accept, and this corpus is evidence about what a *sender* sends. The
inventory measured from a real session (VRC-1) is what the generator is written
from.

What §7 asks this directory to cover when it does arrive: one tracker, three,
eight; a head reference present and absent; position only; rotation only; mixed
messages; a malformed packet; an unsupported address; a duplicate update; a
missing tracker; reordered packets; an OSC bundle; and non-tracker VRChat OSC
traffic interleaved.
