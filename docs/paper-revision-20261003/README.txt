ICDM 2026 review revision snapshot — 2026-10-03
Git tag: icdm2026-review-revision-20261003

Files:
  paper.tex
  fig1_four_layer.png
  fig2_three_gates.png

This is an archival copy of the current working manuscript and its figures.
Continue authoring in the existing document editor; this folder identifies the
paper text accompanying the tagged experiment code.

Status: seven-page review revision. The Teen Research Track limit is five pages,
including references; further editing is required before camera-ready submission.
The generated PDF is not versioned. Compile the source with a standard IEEEtran
LaTeX environment (graphicx, cite, array, url, and listings), or Tectonic.

Reproduce from the repository root:
  Windows PowerShell: .\verify-defects.ps1
  Other environments: python scripts/verify_defects.py

This executes 18 configuration mutants, 18 normal controls (8 distinct inputs),
and 18 explicit repairs. The engine execution path rejects 10 mutants, including
2 rejected by the top-level structural check. The other 8 are accepted by the
engine but distinguished by the external regression oracle. All repairs restore
the baseline observations. This is not an independent-gate ablation or human
review reliability experiment.

Independent human-review protocol (R3-4):
  python scripts/review_semantics.py prepare
This prepares blinded candidates, requirements, supporting definitions, two
blank response forms, and a separate coordinator-only reference key.
Actual accept/reject decisions must be provided by real reviewers. No independent
review responses or agreement measurements are fabricated or included in this
snapshot. See the root README for scoring instructions.

Reproduce the separate beverage protocol/equivalence dataset:
  Windows PowerShell: .\verify.ps1
  Other environments: python scripts/verify_beverage_full.py

The beverage benchmark originated in commit 2e1282f and is included in this tag.
The README at the repository root describes prerequisites and output locations.
