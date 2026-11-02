# 1.0.0

First release of the fork, based on Vertical Conveyor Auto-Connect 1.0.4.

- Generated lifts and direct seams join the existing side's conveyor group, so saves no longer hold split conveyor
  chains at stacked blueprint seams (upstream issue #4).
- Lift previews are created only for endpoints that connect, and unchanged previews are reused, which removes the
  placement freeze that grew with the number of lifts.
- Stays inactive, with an error in the log, when the original mod is installed too.
