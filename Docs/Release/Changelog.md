# 1.0.0

First release, based on Vertical Conveyor Auto-Connect 1.0.4 by ZeeOcho.

- Generated lifts and direct seams join the existing side's conveyor group, so saves no longer hold split conveyor
  chains at stacked blueprint seams (the cause of load crashes).
- Lift previews are created only for endpoints that connect, and unchanged previews are reused, which removes the
  placement freeze that grew with the number of lifts.
- Stays inactive, with an error in the log, when Vertical Conveyor Auto-Connect is installed too.
