# Architecture Documentation Maintenance Guide

## Purpose

This guide describes how to maintain `ARCHITECTURE.md` to keep it synchronized with changes to the QEMU codebase as they are pushed to master.

## When to Update

The architecture documentation should be reviewed and updated when:

1. **Major Subsystem Changes**: New subsystems added or existing ones significantly refactored
2. **Directory Restructuring**: Top-level directories added, removed, or reorganized
3. **New Target Architectures**: Support for new CPU architectures added
4. **Build System Changes**: Significant changes to configure/Meson build system
5. **API Changes**: Major changes to core APIs (QOM, QAPI, TCG, etc.)
6. **Version Updates**: QEMU version number changes

## Update Process

### 1. Monitor Master Branch

Watch for commits to the master branch that might affect architecture:

```bash
git log --oneline origin/master --since="1 week ago" -- \
  accel/ hw/ target/ include/ docs/devel/ meson.build configure
```

### 2. Review Changes

For architectural changes, review:
- Commit messages and linked issues
- Changed files and their documentation
- Developer mailing list discussions
- Release notes

### 3. Update ARCHITECTURE.md

Update relevant sections:

```bash
# Edit the architecture document
vi ARCHITECTURE.md

# Update the "Last Updated" date
# Update the QEMU version if changed
# Update affected sections
```

### 4. Verify Accuracy

Check that documentation matches reality:

```bash
# Verify directory structure
ls -la | diff - <(grep "^\| \`.*\`" ARCHITECTURE.md | sed ...)

# Check target architectures
ls target/ | diff - <(grep target/ ARCHITECTURE.md | ...)

# Verify subsystem locations
```

### 5. Commit Changes

```bash
git add ARCHITECTURE.md
git commit -m "docs: Update architecture documentation for [change description]

References: [commit hash or issue number]"
```

## Key Sections to Monitor

### High Priority

- **Target Architectures**: New CPU support is frequently added
- **Major Subsystems**: Core functionality changes
- **Directory Structure**: Reorganization affects navigation

### Medium Priority

- **Build System**: Changes to configure/Meson
- **Key Technologies**: API updates
- **Acceleration Frameworks**: New accelerator support

### Low Priority

- **Minor version updates**: Keep VERSION field current
- **Reference links**: Ensure external links remain valid

## Review Schedule

- **Weekly**: Check for major architectural changes
- **Monthly**: Review and update version information
- **Per Release**: Comprehensive review before each QEMU release

## Automation Opportunities

Consider automating:

1. **Directory Structure**: Script to generate directory tree from filesystem
2. **Target List**: Extract from `target/` directory
3. **Subsystem Detection**: Parse MAINTAINERS file for new subsystems
4. **Version Sync**: Extract from VERSION file

Example automation script:

```bash
#!/bin/bash
# update-architecture-doc.sh

VERSION=$(cat VERSION)
sed -i "s/\*\*Version:\*\*.*/\*\*Version:\*\* $VERSION/" ARCHITECTURE.md
sed -i "s/\*\*Last Updated:\*\*.*/\*\*Last Updated:\*\* $(date +%Y-%m-%d)/" ARCHITECTURE.md

# Add more automated updates as needed
```

## Collaboration

- **Mailing List**: Subscribe to qemu-devel@nongnu.org for architectural discussions
- **GitLab Issues**: Watch for architecture-impacting issues
- **Pull Requests**: Review PRs for architectural changes
- **Documentation PRs**: Submit updates as separate documentation PRs

## Contact

For questions about architecture documentation maintenance:
- Mailing List: qemu-devel@nongnu.org
- Issue Tracker: https://gitlab.com/qemu-project/qemu/-/issues

## Resources

- [QEMU Developer Documentation](https://www.qemu.org/docs/master/devel/index.html)
- [MAINTAINERS File](../MAINTAINERS)
- [Contributing Guide](https://wiki.qemu.org/Contribute/SubmitAPatch)

---

**Document Version:** 1.0  
**Last Updated:** 2025-11-03
