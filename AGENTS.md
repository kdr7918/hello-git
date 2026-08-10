# Project Development Target

- The final production and deployment target is RHEL 8 with Qt 5.9 and C++11.
- Treat RHEL 8, Qt 5.9, and C++11 as mandatory compatibility constraints for implementation, build, test, and dependency decisions.
- Do not use APIs or language features introduced after Qt 5.9 or C++11 unless a target-compatible fallback is provided.
- Keep platform-specific code compatible with RHEL 8; local macOS verification is supplementary and does not replace target-compatible validation.
