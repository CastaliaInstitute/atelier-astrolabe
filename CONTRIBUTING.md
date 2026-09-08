# Contributing to Atelier

Atelier is developed in GitHub and Codespaces. The repository is the workshop; the Atelier site is the public front door for products, beta access, and community links.

## Start in Codespaces

Open the repository on GitHub and choose **Code → Codespaces → Create codespace on main**. The dev container provides Node.js and the shared editor settings.

To preview the static site locally:

```bash
npx serve . -l 4173
```

Then open the forwarded port in Codespaces.

## Repository conventions

- Name each public product repository `atelier-{project}` (for example, `atelier-mynah` and `atelier-astrolabe`) when it has an independent firmware or hardware lifecycle.
- Give each project repository its own GitHub Pages site at `https://castaliainstitute.github.io/atelier-{project}/`.
- Use GitHub Issues for actionable bugs and tasks.
- Use GitHub Discussions for field notes, questions, ideas, and beta feedback.
- Use Releases for firmware and hardware revision notes.
- Keep customer secrets, Stripe keys, shipping data, and private tester information out of GitHub.
- Use Codespaces Secrets or the deployed server's environment configuration for credentials.

The `project-template/` directory contains the shared GitHub Pages workflow and project-site conventions for new `atelier-{project}` repositories.

## Product maturity

Products use the Atelier orchard stages: **Seedling**, **Sapling**, **In bloom**, **Bearing fruit**, and **Grove**. Show hardware availability separately—for example, `Sapling · Beta hardware available now`.

## Changes

Open a focused pull request. Include screenshots for UI changes and describe how a beta tester or customer is affected.
