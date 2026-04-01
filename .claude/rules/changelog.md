---
paths:
  - "CHANGELOG.md"
---

# CHANGELOG.md Conventions

When updating CHANGELOG.md, follow these rules:

## Format

- Use `## X.Y.Z` (h2) for version headers (not h1)
- Include `_released YYYY-MM-DD_` on the line after the version header
- Group changes under applicable subsection headers (`### Added`, `### Changed`, `### Deprecated`, `### Fixed`, `### Removed`, `### Security`)
- Only include subsections that have entries — omit empty ones
- Each entry is a bullet point with a concise description of the change
- Follow [Semantic Versioning](https://semver.org/)

## When to update

- Add entries to the **Unreleased** section at the top as changes are made on the `next` branch
- Use `## Unreleased` as the section header for accumulated changes that don't yet have a version number
- Do NOT assign a version number — the proper semantic version is determined before merging `next` to `main`
- Do NOT create a new versioned section unless explicitly asked to cut a release

## Example

```markdown
## Unreleased

### Added
- Observability pipeline with `dtf_log()` and `dtf_metric()` APIs
- CBOR wire format for telemetry payloads

### Changed
- BSN PAL functions renamed from `dtf_pal_storage_read_bsn` to `dtf_pal_read_bsn`
```
