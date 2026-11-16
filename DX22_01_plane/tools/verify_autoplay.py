"""Validate real C++ autoplay and attribute deliberate shutdown requests.

Example (run from the repository root with a Windows Python):
  python DX22_01_plane/tools/verify_autoplay.py --label check --exe x64/CleanBuildDebug/DX22_01_plane.exe
This CLI launches only the explicitly named executable in fresh isolated folders.
"""
from autoplay_validation.campaign import main


if __name__ == "__main__":
    raise SystemExit(main())
