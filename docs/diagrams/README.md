# Diagrams

The architecture diagram in the top-level README is generated from the Mermaid
sources in this folder, in light and dark variants selected by
`prefers-color-scheme`.

They are committed as images so they render everywhere, including the GitHub
mobile app, which does not render Mermaid.

Regenerate after editing a `.mmd` source:

```bash
npx -p @mermaid-js/mermaid-cli mmdc -i system-architecture-light.mmd -o system-architecture-light.png -b "#FFFFFF" -s 2
npx -p @mermaid-js/mermaid-cli mmdc -i system-architecture-dark.mmd  -o system-architecture-dark.png  -b "#0D1117" -s 2
```
