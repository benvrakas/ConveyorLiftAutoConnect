# 1.1.2

- Fixed: stacking a blueprint onto another blueprint connected only some of the lifts. The game's position rounding
  left the lift holes up to a few millimetres apart, more than the mod allowed; they now line up within 2 cm.

# 1.1.1

- Fixed: large blueprints (for example a stack of six blenders) could not be placed onto another blueprint while
  auto-connect was on. The mod's seam data made the placement request too big for the game to send; it now sends
  only the seams that connect.

# 1.1.0

- Fixed: placing a blueprint onto blueprints from an earlier session (anything loaded from a save) connected nothing
  at most map positions. Saved positions are slightly rounded, and endpoints had to line up exactly; they now count
  as lined up within 1 mm.
- New settings page (Mods menu and Satisfactory Mod Manager): turn auto-connect off, choose which lift sets the tier
  (blueprint side first, faster lift, or world side first), cap the length of generated lifts, and turn on a detailed
  log for bug reports.
- Rewritten internally into smaller parts, with one undo path for every seam that fails to link. Placement works
  the same.

# 1.0.0

First release, based on Vertical Conveyor Auto-Connect 1.0.4 by ZeeOcho.

- Generated lifts and direct seams join the existing side's conveyor group, so saves no longer hold split conveyor
  chains at stacked blueprint seams (the cause of load crashes).
- Lift previews are created only for endpoints that connect, and unchanged previews are reused, which removes the
  placement freeze that grew with the number of lifts.
- Stays inactive, with an error in the log, when Vertical Conveyor Auto-Connect is installed too.
