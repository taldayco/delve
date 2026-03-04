Review:

https://stripe.dev/blog/minions-stripes-one-shot-end-to-end-coding-agents
and
https://stripe.dev/blog/minions-stripes-one-shot-end-to-end-coding-agents-part-2

I want to take inspiration from applicable information in these articles and put some of the ideas to use for this project.

IMPORTANT:

> We DO NOT want slack integration or devboxes
> What we DO WANT: Blueprints, Custom Harness, github integration.

Important paragraphs from the articles:
Important section 1:

"Minions are orchestrated with a primitive we call “blueprints.” Blueprints are workflows defined in code that direct a minion run. Blueprints combine the determinism of workflows with agents’ flexibility in dealing with the unknown: a given node can run either deterministic code or an agent loop focused on a task. In essence, a blueprint is like a collection of agent skills interwoven with deterministic code so that particular subtasks can be handled most appropriately.

In the blueprint that powers minions, for example, there are agent-like nodes with labels such as “Implement task” or “Fix CI failures.” Those agent nodes are given wide latitude to make their own decisions based on input. However, the blueprint also has nodes with labels such as “Run configured linters” or “Push changes,” which are fully deterministic: those particular nodes don’t invoke an LLM at all—they just run code.

Thus, blueprints are a way to guarantee certain subtasks are completed deterministically within the agentic run. The minion blueprint ends up looking like a state machine that intermixes deterministic code nodes and free-flowing agent nodes.
"
Important section 2:

"
In a large codebase such as Stripe’s, an agent set loose without any guidance might encounter trouble following best practices or using the proper libraries, even with good linters. To help with this issue, various agent rule formats—think CLAUDE.md or AGENTS.md—allow agents to “learn” about the codebase automatically as they traverse its directory structure.

Due to the size of our repositories, we use unconditional global rules very judiciously, since otherwise the agent’s whole context window would fill with rules before the agent even starts. Instead, we almost exclusively give minions context from files that are scoped to specific subdirectories or file patterns, automatically attached as the agent traverses the filesystem.

From our perspective, it’s best to avoid duplication of rule files in favor of our agent reading the same context that human-directed agents use. Given that, we standardized on a popular rule format that supported these features—Cursor’s—and modified our harness to allow minions to read those rules in addition to a previous homegrown format.

We also now sync our Cursor rules into a format that Claude Code can read as well, so that our three most popular coding agents (minions, Cursor, and Claude Code) can all benefit from the guidance that lives in rule files that Stripe engineers are scaffolding in our codebase.
"

Imporant section 2:
"
Reading from a filesystem works well for static context gathering, but agents frequently need to dynamically fetch information using networked tool calls. In particular, to fully hydrate user requests, minions need to retrieve information such as internal documentation, ticket details, build statuses, code intelligence, and more. Upon release, the Model Context Protocol (MCP) quickly became the industry-wide standard for networked tool calls, and we moved to integrate minions with it.

Stripe has built or integrated lots of agents running on different frameworks: a no-code internal agent builder, custom agents running on dedicated services, third-party off-the-shelf agents, command-line agentic tools and other coding agents, and agentic Slack bots. All these agents, not just minions, needed MCP capabilities, often including overlapping sets of common tools.

To support all of these, we built a centralized internal MCP server called Toolshed, which makes it easy for Stripe engineers to author new tools and make them automatically discoverable to our agentic systems. All our agentic systems are able to use Toolshed as a shared capability layer; adding a tool to Toolshed immediately grants capabilities to our whole fleet of hundreds of different agents.

Toolshed currently contains nearly 500 MCP tools for internal systems and SaaS platforms we use at Stripe. Agents perform best when given a “smaller box” with a tastefully curated set of tools, so we configure different agents to request only a subset of Toolshed tools relevant to their task. Minions are no exception and are provided an intentionally small subset of tools by default, although per-user customizability allows engineers to configure additional thematically grouped sets of tools for their own minions to use.

Since minions operate autonomously with full freedom to call their MCP tools, we also have an internal security control framework that ensures they can’t use their tools to perform destructive actions. As a first line of defense, though, our devboxes already run in our QA environment, and consequently, minions don’t have access to real user data, Stripe’s production services, or arbitrary network egress. This is no accident: we built isolated devboxes deliberately, so humans have an environment they can experiment within safely. But, as with so much else, a development environment that’s safe for humans has proven to be just as useful for minions.
"

Imporant Section 3:
"
While we build minions with the goal of one-shotting their tasks, it’s key to give agents automated feedback that they can iterate against to make progress. Stripe’s enormous preexisting battery of tests—over three million of them—can provide this feedback. However, while a pushed branch will run all relevant tests in CI, we don’t want to rely too heavily on CI for all our code feedback.

We try to operate under the principle of “shifting feedback left” when thinking about developer productivity. That phrase means that if we know an automated check will fail CI, it’s best if it’s also enforced in the IDE and presented to the engineer right away, since that’s the fastest way to provide feedback to the user.

For example, we have pre-push hooks to fix the most common lint issues. A background daemon precomputes lint rule heuristics that apply to a change and caches the results of running those lints, so developers can usually get lint fixes in well under a second on a push.

Minions naturally integrate with this framework as well, so they don’t have to waste tokens or CI minutes by iterating against an auto-formatter or similar. We run a subset of linters as a deterministic node within the agent devloop blueprint, and loop on that lint node locally before pushing an agent’s branch, so that the branch has a fair shot at passing CI the first time around.

It’s infeasible to run all tests locally, so we also include one iteration against the full CI suite into the standard minion blueprint. After a minion pushes a change, we run CI and auto-apply any autofixes for failing tests. If there are failures with no autofix, we send the failure back to a blueprint agent node and give the minion one more chance to fix the failing test locally. After the second push and CI run, we send the branch back to its human operator for manual scrutiny.

Why have only one or two rounds of CI? There’s a balancing act between speed and completeness here; CI runs cost tokens, compute, and time, and we think there are diminishing marginal returns if an LLM is running against indefinitely many rounds of a full CI loop. We feel that our policy strikes a good balance between the competing considerations here.
"

Now how does this all relate to delve?

Well, delve is a custom game engine that needs to prioritize scalability, and decoupled modules that can be reused in other projects - its a game engine after all.

Because of this, the project will eventually have too many large files for a single agent to efficiently work through by themself.

Additionally since this is a game engine, most of the analysis for whether something is working or not is currently done by humas qualitatively. We need to change this so that everything that happens on the screen during execution is quantitative such that our agents can "see" what is happening when the executable is running and have the same information a human would when viewing it with their eyes.

Agents dont just need a way to see the information as its displayed to the screen, they need a way to interact with the application. after running the application, they need to test via input whether the feature or bug fix they were working in actually did work. For example, if an agent was tasked with fixing an issue with the light shaders, they should be able to run the application and quantitatively know that their fix worked.

For situations like agenting "viewing" and testing the way a human would, we need to define a custom blueprint. We should have custom blueprints for workflows such as this, and even a Meta blueprint that is called when an unfamiliar task is presented to the main orchestration agent that allows them to build a deterministically useful and quality blueprint for the task given.

Since the project will be large, we need to ensure that we have the tools in place for agents to gather context about the project efficiently. No agent should have more than 40% context used at any one point. This shouldn't be a killswitch, just a guideline that they'll need to compact their context at the next convenient point before continuing, or prompting another agent.

we should set up custom agent feedback loops, tools, and other tools that our agents can utilize to work on this project effectively. Agents SHOULD NOT have access to all tools whenever they are made. They should only have acess to the tools that they need to do their job. If a worker agent does not have the required tools to accomplish a task, it should (if it is not a meta agent) prompt a meta agent to create an agent whose sole purpose it is to complete that task with extrememly limited tools limited to ONLY the tools they need to accomplish the task.

Agents should NOT create vague outputs, prompts, and should prompt the user to explain more in depth if their understanding of the current proplem is vague. everything from agents, to blueprints, to hooks will not be vague.

We also need to ensure that the system is token efficient, and does not contain any logical bugs that will keep background processes in forever, overwrite itself, etc.

We also need to ensure that the system can scale itself as the project grows and becomes more complex. It should be self cleaning, self updating, and self regulating. Decoupling and optimization should also be built into the system. If a domain grows too large for one agent to handle by itself, it should ask a smarter agent with more capabilities to decouple the domain responsibilities so that agents can continue working on the system without becoming overwhelmed in context. There should also be decoupling and optimiziation built into this system project wise. For example we could either have new pipelines commands built into the system or a blueprint/agent specification in place so that are currently not decoupled but should be for scalability are, and that future systems that should be build as decoupled systems are built this way.
