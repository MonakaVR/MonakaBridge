# WS-00 C01 citation correction carried with WS01a

This corrects the C01 evidence in the sealed WS-00 P0 audit. That historical
archive remains unchanged. No protocol source, schema, codec or normative
publisher identity contract is changed by WS01a.

At accepted MVIP commit `6dfa595fa308fea64880fb5f501073feed9a726a`, the
previously quoted native
[codec.cpp:117](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/cpp/src/v2/codec.cpp#L117)
is inside `authoritySemantics` (function starts at line112). The corresponding
[JVM MonakaCodec.kt:161](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/jvm/src/main/kotlin/dev/monaka/protocol/v2/MonakaCodec.kt#L161)
is also authority semantics. Their nonblank check is not evidence of the MTP
tracking publisher validator.

The MTP tracking schema's
[Id definition:14](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/schema/v2/monaka-tracking.schema.json#L14)
requires a nonempty string, maximum96 code points, excluding C0/C1 controls.
It permits spaces, including an all-space nonempty string. MTP
[publisher_id:631](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/schema/v2/monaka-tracking.schema.json#L631)
references that Id definition (tracker-state counterpart at line738).

The native decode path dispatches MTP at lines92–93 and runs the generic schema
validator before message semantics at
[codec.cpp:199](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/cpp/src/v2/codec.cpp#L199).
Generic Id reference handling at line39 additionally bounds UTF-8 bytes to96;
generic string validation at lines55–70 applies length/control rules. The
authority-only branch at line133 does not send MTP through `authoritySemantics`.
JVM's generic Id reference check is at
[MonakaCodec.kt:79](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/jvm/src/main/kotlin/dev/monaka/protocol/v2/MonakaCodec.kt#L79).
These are source-inspection findings, not a new native/JVM validator execution.

The normative statement in
[C2.md:7](https://github.com/MonakaVR/MonakaVRInputProtocol/blob/6dfa595fa308fea64880fb5f501073feed9a726a/docs/C2.md#L7)
still defines publisher_id as a persistent Bridge installation identity.
Syntactic acceptance does not establish permission or producer-neutral meaning
for an independent publisher. **C01 remains UNRESOLVED**, requiring protocol-owner
interpretation or a separately reviewed compatibility proposal. WS01a makes no
independent-publisher legitimacy claim and does not fabricate an installation
identity to bypass that gate.
