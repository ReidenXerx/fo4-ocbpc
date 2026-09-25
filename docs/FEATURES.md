# fo4-ocbpc: every feature

`cbp.dll`, OCBPC (OpenCBP physics with collisions, for Fallout 4) as extended for **Anatomy** and
**Rapport**. A drop-in replacement for OCBPC's own `cbp.dll`: your `ocbp.ini` and
`OCBPCollisionConfig.txt` keep working unchanged.

Game: Fallout 4 **1.10.163** (Steam or GOG, byte-identical at every address used) with **F4SE 0.6.23**.
Not the next-gen update: every hook reads the game's code before patching it and stays off, logged,
on any other build.

Markers: **[alone]** works with just this DLL; **[Rapport]** needs Rapport; **[Anatomy config]** is
switched on by Anatomy's `F4SE\Plugins\Anatomy\ocbp.ini` (or the same keys in your own).

---

## 1. Better collision for everyone

- **Each collider pushes once per frame** [alone]. OCBPC's spatial grid put a collider in every cell
  it touched, and a bone was pushed once per cell, so near a 25-unit cell edge a push doubled and
  popped back the next frame. Every OCBPC collision (breasts, butt, anything your config collides) is
  now pushed once.
- **A penis collides as one continuous tube** [Anatomy config, `[Tube]`]. OCBPC collides ball
  against ball and adds every overlapping pair's push; a penis is five balls about 3 units apart with
  gaps between them, so the flesh rode in and out along a thrust and the opening parted before the
  tip arrived. The tube's radius runs smoothly from bone to bone, with a measured glans profile.
  Measured on BodyTalk4: thrust wobble at the anus 0.69 -> 0.21, vagina 0.26 -> 0.13.
- **Toys collide as one tube too** [Anatomy config, `[Tube] props=1`]. A lip against a toy rested
  4.96-5.05 from its axis where its surface is 3.10; as a tube it rests exactly on the surface (3.100).
- **Held props are colliders** [Anatomy config, `[Props]`]. Whatever an animation hangs on a hand's
  AnimObject node (a dildo, a bat) becomes a line of collision spheres along its rendered length.
  `targets=` limits what a prop may push, so a mug held at the chest pushes nothing. Weapons are
  excluded.
- **Stretch groups** [Anatomy config]. An opening can widen around something bigger than a penis
  (a fist, a toy): a group of bones takes the smallest cross-push among its members and, past a knee,
  moves a `<bone>_Stretch` child out by gain x (push - knee), capped.
- **A penis never pushes its own owner; a toy pushes only its `[Props]` targets**, its holder's own
  included (solo scenes).

## 2. Genital bones with no skeleton patch

- **Run-time bones** [Anatomy config, `[Bones]`]. For every skinned mesh that names a configured bone,
  the missing bones are created under the actor's real `Pelvis_skin` and the skin is pointed at them.
  No skeleton file is shipped or overwritten; a skeleton that already has them is left alone.
  Steady-state cost: one comparison per skin.
- **Layered config.** Your `ocbp.ini` is read first, then `Data\F4SE\Plugins\Anatomy\ocbp.ini`;
  collision files are appended the same way (a node both files list keeps its spheres and gains
  Anatomy's). Installing or updating Anatomy never overwrites your physics config.

## 3. The penis finds its opening (aim and shape)

All [Anatomy config, `[Aim]` / `[Shape]`], and only between actors AAF marks as busy in a scene.

- **Auto-aim** into the vagina, anus or mouth the animation implies: the `Penis_00..05` chain turns
  toward the nearest opening (lock turn <= 35 deg, held <= 45, entry within 75 deg of the opening's
  axis) and a shaft that is slightly short stretches up to 10%.
- **The shaft bends inside her** along a measured canal path, instead of poking straight through the
  body.
- **Deep throat follows the neck**: the throat path is split into a HEAD part and a Neck part, so the
  shaft stays in the throat when the head tilts back.
- **Mouth entry below the lip line** and straightened in front of the lips, so the shaft no longer
  rides up into the cheek or clips a corner.
- **Hand grips**: a curled hand around the shaft is a target of its own (the grip's centre is fitted
  from each finger's curl), so a hand job is aimed at the hand, not at an opening behind it.
- **Exact per-frame bend**: no lag behind a fast pull-out; switching openings crossfades.
- **Mushroom shape, per man** (`[Shape]`): a thinner shaft (x0.85) and a bigger head (x1.2-1.25,
  fixed per man from his form id), scaled on the bones at run time. BodyTalk4's files are untouched,
  and joints stay where the animation put them.
- **Genital depth** is measured while locked and feeds the deep face (section 5).

## 4. The mouth

All [Anatomy config, `[Mouth]`]. Fallout 4 heads have no mouth bones: the mouth is expression morphs.

- **The mouth opens to what is at the lips**, over the animation's own face: Jaw Open, the lip
  funnels and Upper Lip Up are written after the engine's merge, and hand back to the animation when
  nothing is there (held 0.35 s between strokes).
- **Lips fit the cross-section of what is inside**: a measured table of how each mouth morph moves
  the inner lip at 7 points is solved every frame, so lips hug a shaft or a finger instead of gaping.
- **Mouth corners** open out around a wide shaft and draw in ("hug") around a narrow one, never
  ending inside it.
- **Quick lips**: lips and corners follow at their own rate (60/50 per second), re-closing between
  strokes.
- **Reads the real width** of a penis or toy, glans included, not its thinner collision balls.
- **The face reacts** (`face=` terms): brows, cheeks and nose rise with contact, depth and stroke
  speed, only ever raising what the face already has.

## 5. Faces: Rapport's authority

[Rapport] for every item; [alone] means only this DLL is needed on the engine side.

- **Rapport's face is the one that shows** in any AAF scene [alone]: a face Rapport holds is written
  after the engine's own merge, so it can close eyelids and a jaw an animation opened, which an
  expression override never can. Blinks still close the eyes.
- **Spoken lines keep their lip sync** [alone]: while the engine plays a line, the mouth goes back to
  its lip sync for exactly that line (read from the engine's lip-sync state).
- **Smooth changes** [alone]: a held face eases over 250 ms to a new one, and eases out over 250 ms
  when released.
- **Deep face**: brows draw in as penetration gets deeper, oral and (with `[Aim]`) vaginal and anal.
- **Glances**: during a scene she looks up into her partner's eyes now and then, for as long as Rapport
  asks, with the eyelids she should have. Fallout 4 eyes have no bones: the engine slides the eye
  texture, and this hooks that update. Needs `[Eyes] glances=1` (Anatomy's config sets it).
- **A face for the glance** (a look she gives only while looking at him), and **eye rolls**.
- **Rapport's MCM tunes the engine live**: lip clearance and speed, shaft and head size, reaction
  strength, deep face, each on or off. A setting can only turn off what the ini turned on.
- **The protocol** (F4SE messages, `RFAS` set, `RFAC` clear, `RFAD` deep, `RFAK` knobs, `RFAG`
  glance, `RFAX` glance face, and the engine's `RFAH` hello with a feature bit per capability) is in
  `CBPSSE/FaceAuthority.h`. Rapport relies on a feature only once the hello says it is live.

## 6. Stability and diagnostics

- **Every hook checks the game's code first** [alone] (the face merge, its one caller, the eye update
  and its two callers) and stays off, logged, on any other build. The merge's call is repointed
  through F4SE's branch trampoline; the engine's own code is never modified.
- **Guarded installs** [alone]: a fault while installing the eye or the mouth hook turns that one
  feature off and logs where, instead of crashing the game with no crash log.
- **The DetourXS x64 fix**: the hook library decoded x64 code as 32-bit and could cut an instruction
  in two (it crashed every save load while the mouth still used it; the mouth no longer does).
- **A log two game processes cannot break** [alone]: a second Fallout4.exe (a relaunch after a crash)
  used to truncate the running game's log; now it rotates it aside or writes its own.
- **Discovery log** `Documents\My Games\Fallout4\F4SE\anatomy_ocbpc.log` [alone], last four runs
  kept: props turned into colliders, the bones added, the mouth, the aim's lock and release reasons,
  every hook's install result. Each line is written once per run (about 20 lines a minute in a real
  session, most at load), capped at 4000 with a notice. For modders, `[Log] discover=1` also lists
  genital- or toy-looking nodes on nearby actors, to name an unknown creature's bones; it walks their
  scene graphs every 2 s, so it is off unless you turn it on.
- **Builds with today's compiler** (VS 2022, v143), statically linked, the same imports and exports as
  the original.

## 7. Tests

Offline suites under `tests/`, each proven by planted mutations (every planted fault must fail it):
`tests/aim` (22 cases), `tests/face` (face authority, 51 mutation checks), `tests/lips` (17 cases),
`tests/tube` (8 cases).

## Settings reference

Every key, its default, its clamp and what Anatomy ships: see the section list in
[`README.md`](../README.md#configuration) and the header comments of `CBPSSE/Mouth.h`, `Aim.h`,
`Eyes.h`, `Bones.h`, `TubeCollide.h` and `config.cpp`.
