# WhisPrompt AI Coding Workflow — Reference Description

**Captured:** August 22, 2026  
**Primary source:** https://whisprompt.ai/ai-coding-workflow  
**Purpose:** Product/interaction reference for Astrolabe Agent Dial and related physical AI-control concepts.

> This document summarizes and analyzes the public WhisPrompt product page. It is not affiliated with WhisPrompt and does not reproduce the page verbatim. Product details may change as the product moves through beta.

## 1. Executive Summary

WhisPrompt describes itself as an **AI Workflow Controller**: a palm-sized physical voice-and-touch device paired with a desktop client. Its intended purpose is to shorten the human side of an AI workflow by giving repeated actions—prompting, revising, switching tools, returning to an agent, reviewing state, and approving actions—a fixed physical location.

The product is positioned as complementary to the keyboard and mouse rather than as a replacement. The basic thesis is that AI systems can perform increasingly long-running or multi-step work, but human interaction with those systems still involves friction: typing long prompts, clicking into the right input field, switching among applications, watching for completion, finding the relevant window again, and responding to permission or approval requests.

WhisPrompt tries to collapse those interactions into a handheld control loop:

**Speak → direct the input → let the agent work → receive state/attention feedback → return to the correct tool → revise or approve → continue.**

The notable element is not voice dictation alone. The device is backed by a **state-aware desktop client** that attempts to know where words should go, which controls correspond to which actions, and whether supported agent work is running, waiting for approval, or finished.

## 2. Product Concept

The physical product is presented as a compact controller designed around four interaction classes:

1. **Voice input**
2. **Physical shortcuts/actions**
3. **Window/tool navigation**
4. **Confirmation/approval**

WhisPrompt's design goal is to keep the next common AI action physically available without requiring the user to return to menus, the mouse, or repeated keyboard shortcuts.

The marketing emphasizes giving an AI a complete verbal brief in one pass—including the task, context, constraints, and expected result—rather than slowly typing a prompt.

The product claims this can substantially accelerate prompt input. The page advertises "3× faster input," although the public page should be treated as a marketing claim rather than an independently validated benchmark.

## 3. Physical Controls

The public page identifies the following hardware controls.

### Primary Action Button

The primary button is configured by default as **hold-to-speak**.

The expected interaction is:

- press/hold the button;
- speak the desired prompt or instruction;
- release when finished;
- have the resulting transcription directed into the intended software input.

This makes push-to-talk the central physical gesture of the device.

### Action Button 1

The default/promoted role of Action Button 1 is to **return to the coding agent**.

This is important because it makes focus restoration a first-class workflow operation. The user can be examining output, documentation, a browser, an editor, or another application and use a fixed button to return to the active AI coding interface.

### Action Button 2

Action Button 2 is described as **assignable to shortcuts and workflow actions**.

The broader product description says controls and gestures can be customized, so this button functions as a programmable workflow control rather than a fixed command.

### Wheel

The wheel is used for:

- opening/switching among selected windows;
- navigating the window switcher;
- customization.

The conceptual difference from a generic system task switcher is that the user can keep a chosen set of relevant working windows "within reach."

### USB-C Port

The page states that USB-C is used for charging and data transfer.

## 4. Voice Input Model

Voice input is the entry point to the workflow, but WhisPrompt presents several layers beyond ordinary speech-to-text.

### Full-Prompt Dictation

The product encourages users to speak a complete instruction in one pass, including task, context, constraints, and expected result. The underlying design assumption is that natural speech makes it easier to express richer instructions than keyboard entry, especially for detailed AI prompts.

### Low-Voice / "Whisper" Use

The page explicitly promotes the ability to speak prompts quietly when other people are nearby. This is a usability positioning rather than a fundamentally different interaction model, but it is significant because the device is intended for ordinary desks and shared environments rather than only private voice-assistant scenarios.

### Local and Cloud Voice Paths

WhisPrompt states that voice recognition can be routed in two ways:

**Local processing**
- runs on the user's computer;
- keeps dictation available without Wi-Fi;
- is positioned as the privacy-sensitive option.

**Cloud processing**
- can be selected when connectivity is available;
- is described as providing a more capable/"smarter" voice experience.

The public page says the device purchase includes local voice input plus a "generous" cloud voice allowance.

## 5. Smart Input Targeting

A key part of the product is **input targeting**.

The WhisPrompt client is described as knowing where the user's words are intended to go and placing transcription into the appropriate active input field.

This addresses a common voice-dictation problem: even if transcription is accurate, the user still has to click the desired field before speaking.

The intended experience is therefore not merely **speech → clipboard** but something closer to **speech → identify current workflow target → inject text into intended application/input.**

This makes application and focus awareness an important part of the architecture.

## 6. Whismart: Spoken Revision of Existing Text

The page describes a feature called **Whismart**.

Whismart reads the text before the cursor in the active input field. The user can verbally describe how that text should change—for example, shorten it, expand it, change the tone, or restructure it. Whismart then replaces the passage in place.

This creates a second voice interaction mode distinct from fresh dictation:

- **Fresh prompt mode:** Speak what should be entered.
- **Revision mode:** Speak what should change about existing text.

This distinction is particularly relevant for agent workflows because iterative steering is often more important than generating the original prompt.

## 7. Window and Tool Switching

WhisPrompt treats context switching as a primary AI-workflow problem.

The user selects active/relevant windows they want readily available. From the physical device they can open the window switcher, move through choices using the wheel, and activate the required application/window.

The page repeatedly emphasizes returning to the relevant AI coding tool after inspecting a result elsewhere.

A representative workflow is:

**agent → generated result → inspect elsewhere → physical action to return to agent → speak next instruction.**

The intention is to preserve cognitive continuity: the next instruction can be issued while the result and user's judgment are still fresh.

## 8. Agent Status Awareness

WhisPrompt's desktop client is described as remaining aware of supported agent work. It exposes states such as **running**, **waiting**, and **done**.

The page also specifically discusses detecting when agent work finishes or requires approval. The client therefore functions as more than a macro/shortcut utility. It is intended to maintain some model of the active agent's lifecycle.

This state can trigger a notification and allow the user to return directly to the correct tool.

## 9. Attention and Notifications

WhisPrompt is designed around the fact that agentic AI work can continue asynchronously from the user's immediate interaction.

Rather than requiring the user to keep watching the AI window, the system can alert them when an agent has completed work or is blocked waiting for approval. The device/client combination then provides a route back to the relevant task.

The product's workflow therefore alternates between:

- **Human-active phase:** The user provides intent, corrections, or a decision.
- **Agent-active phase:** The AI performs work while the user can attend to something else.
- **Attention transition:** WhisPrompt signals when human involvement is required again.

This "attention handoff" is one of the strongest conceptual aspects of the product.

## 10. Approval / Confirmation

The product explicitly makes **approval** part of the physical interaction loop.

The advertised sequence is approximately:

1. AI performs analysis/work.
2. WhisPrompt notifies the user.
3. User states or decides what should happen.
4. User confirms the action from the device.

The page describes approval as one of the recurring AI workflow operations that deserves a dedicated physical control.

The public material does not provide a full technical permission model, nor does it enumerate all actions that may be approved. The important design point is that **agent state requiring user authorization becomes a physical notification and interaction**, rather than simply another modal dialog hidden on the desktop.

## 11. WhisPrompt Agent Client

The desktop software is central to the system. The public page attributes four main capabilities to the client.

### Reliable Voice Input
- local recognition when offline;
- optional cloud recognition when available.

### Smart Input Targeting
- understands where transcription is meant to land;
- inserts text into the appropriate active input field.

### Live Work State
- tracks supported agent tasks;
- exposes whether work is running, waiting, or done.

### Custom Controls
- lets users assign shortcuts and AI workflow actions;
- maps these operations to physical buttons and gestures.

The client is described as staying "in the loop" with the real state of the computer. This is effectively the software integration layer between generic physical controls and application-specific workflow state.

## 12. End-to-End Workflow Examples

### A. Rough Idea → AI Draft

1. User presses the voice control.
2. User speaks unstructured/raw thinking.
3. The text is sent to an AI.
4. The AI turns it into a draft.

The value proposition is that the original thought can remain rich and conversational rather than being compressed into a short typed prompt.

### B. AI Draft → Spoken Revision

1. User reviews the result.
2. User invokes Whismart.
3. User describes the desired change verbally.
4. Existing text is rewritten in place.
5. User continues judging and refining rather than manually editing every sentence.

### C. Result → Next Agent Prompt

1. The user examines generated work.
2. A physical action returns focus to the coding agent.
3. The user immediately dictates the next step.
4. Iteration continues without manually navigating back through windows.

### D. Long-Running Analysis → Human Decision

1. Agent performs work.
2. User attends to something else.
3. WhisPrompt signals that work is ready or blocked.
4. User returns to the correct agent/task.
5. User states a decision or instruction.
6. User confirms the action physically.

These scenarios reveal the product's broader model: **capture intent quickly, reduce navigation, preserve context, and minimize the latency of human intervention.**

## 13. Relationship to Keyboard and Mouse

WhisPrompt explicitly does **not** present itself as a keyboard/mouse replacement. Traditional editing and navigation remain on conventional peripherals.

The controller is intended for a narrower category of interactions where physical immediacy or voice is advantageous: lengthy prompts, repeated AI commands, returning to an AI interface, switching among a small working set of tools, redirecting/revising, responding to agent attention requests, and approving next steps.

This positioning is useful because it avoids forcing all desktop interaction through a novel device.

## 14. Relationship to Voice Assistants

WhisPrompt distinguishes itself from conventional voice assistants by emphasizing **workflow state** rather than command-response conversation.

A normal voice assistant is conceptualized as:

**wake/speak → command → response**

WhisPrompt's intended model is closer to:

**work context → speak → AI task continues → inspect/revise → agent waits → approve → resume.**

The controller therefore exists inside a longer-lived computational process rather than treating each utterance as an isolated command.

## 15. Tool Compatibility

The public page says WhisPrompt is designed to work with existing AI tools, including AI coding tools, productivity applications, and browser-based workflows.

The page notes that compatible tools and supported actions are expected to expand during beta.

Earlier/related WhisPrompt promotional material specifically associates the concept with modern coding-agent workflows such as Claude Code, Codex, Copilot, Cursor, and similar tools. Exact integration depth should be verified against current product documentation before assuming a particular agent API or feature is supported.

The architecture described publicly appears designed around a **desktop integration/client layer**, allowing the hardware to remain relatively generic while the client handles application-specific behavior.

## 16. Customization

WhisPrompt promotes programmable controls. Users can assign shortcuts, AI actions, and workflow operations to buttons and gestures.

This means the hardware interaction grammar is intended to be configurable rather than tightly hard-coded to one application. The wheel also participates in customization as well as switching windows.

## 17. Business / Product Model

The page says:

- the core experience does **not require a subscription**;
- the physical device is a one-time purchase;
- the device includes the WhisPrompt Agent Client;
- local voice input is included;
- a cloud voice allowance is included.

The site was still describing the product in reservation/beta terms at the time this reference was captured, and states that compatibility, pricing, launch details, and supported actions may evolve.

## 18. Core Interaction Grammar

Abstracted from the marketing language, WhisPrompt can be represented as the following control loop:

```text
                  ┌─────────────────┐
                  │   Human intent  │
                  └────────┬────────┘
                           │
                     Hold + speak
                           │
                           ▼
                ┌─────────────────────┐
                │ WhisPrompt Client   │
                │                     │
                │ • transcribe        │
                │ • locate target     │
                │ • retain mappings   │
                │ • observe agent     │
                └─────────┬───────────┘
                          │
                    inject / command
                          │
                          ▼
                  ┌───────────────┐
                  │ AI / Agent    │
                  │ Application   │
                  └──────┬────────┘
                         │
               work / output / request
                         │
                         ▼
                ┌──────────────────┐
                │ Client observes  │
                │ agent state      │
                └───────┬──────────┘
                        │
             ┌──────────┴───────────┐
             ▼                      ▼
           DONE                NEEDS INPUT
             │                      │
       notify user            notify user
             │                      │
             └──────────┬───────────┘
                        ▼
              switch/focus correct
                   application
                        │
              ┌─────────┴────────┐
              ▼                  ▼
          speak change        approve
              │                  │
              └─────────┬────────┘
                        │
                        ▼
                  continue loop
```

The physical controller reduces friction specifically at the **human↔agent boundary**.

## 19. What Is Technically Distinctive

None of WhisPrompt's individual ingredients is unprecedented: speech-to-text, macro pads, rotary application switchers, programmable buttons, desktop notifications, and AI coding-agent permission systems all already exist.

The distinctive product idea is their **integration into a persistent physical workflow controller**.

Its strongest design choices are:

1. **Push-to-talk is deterministic.** The user explicitly indicates when speech is intended for the system.
2. **The software knows the target.** The user should not repeatedly establish application focus manually.
3. **Revision is a different operation from new input.** Spoken transformations can modify existing content.
4. **Application switching is constrained to the current working set.** The controller serves task context, not the whole operating system.
5. **Agent lifecycle is represented.** Running/waiting/done is treated as first-class state.
6. **Attention transitions are explicit.** The user can disengage while an agent works and be recalled when needed.
7. **Approval is physically available.** Human authorization is part of the controller's core vocabulary.
8. **Hardware remains relatively generic.** A desktop client appears to carry most application semantics.

## 20. Reference Implications for Astrolabe

The following section is interpretation for design reference, not a claim about WhisPrompt itself.

### A. Do not implement only voice dictation

The important system is **voice + context + application targeting + task state + attention + approval.** Simply sending microphone audio to speech recognition would reproduce only a small portion of the value.

### B. Put semantic state in the host bridge

WhisPrompt's desktop-client concept supports the architectural choice that Astrolabe firmware should remain agent-agnostic.

A Mynah/Astrolabe bridge can own active project, selected repository, active agent, thread/session, task state, host application/window, pending approval, and notification routing.

### C. Preserve task identity

A physical controller becomes substantially more useful when a new utterance can mean **"continue/change this task"** rather than always **"start a new prompt."**

### D. Treat attention as a resource

Agent completion and approval requests should