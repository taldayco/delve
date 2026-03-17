#!/usr/bin/env node

import { encode } from './toon.mjs';

const VIKING_URL = 'http://127.0.0.1:1933';
const FETCH_TIMEOUT = 4000;
const PROJECT_ROOT = '/home/neto/Projects/delve/';
const PROJECT_URI = 'viking://resources/delve/';

const DIRECTORY_MAP = [
  ['src/game/terrain/', 'viking://resources/delve/terrain/'],
  ['src/game/render/',  'viking://resources/delve/actor/'],
  ['src/shaders/',      'viking://resources/delve/shader/'],
  ['src/engine/',       'viking://resources/delve/engine/'],
  ['src/test/',         'viking://resources/delve/test/'],
  ['src/game/',         'viking://resources/delve/game/'],
];

// --- HTTP helpers ---

async function viking(method, path, body) {
  const url = `${VIKING_URL}${path}`;
  const opts = {
    method,
    signal: AbortSignal.timeout(FETCH_TIMEOUT),
    headers: { 'Content-Type': 'application/json' },
  };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(url, opts);
  const json = await res.json();
  if (json.status !== 'ok') throw new Error(json.error || 'viking error');
  return json.result;
}

async function contentAbstract(uri) {
  return viking('GET', `/api/v1/content/abstract?uri=${encodeURIComponent(uri)}`);
}

async function contentOverview(uri) {
  return viking('GET', `/api/v1/content/overview?uri=${encodeURIComponent(uri)}`);
}

// Basic vector similarity search (no session context needed)
async function find(query, limit, targetUri) {
  const body = { query, limit };
  if (targetUri) body.target_uri = targetUri;
  return viking('POST', '/api/v1/search/find', body);
}

async function fsLs(uri) {
  return viking('GET', `/api/v1/fs/ls?uri=${encodeURIComponent(uri)}`);
}

// --- Path / URI mapping ---

function pathToUri(absPath) {
  if (!absPath.startsWith(PROJECT_ROOT)) return null;
  const rel = absPath.slice(PROJECT_ROOT.length);
  for (const [prefix, uriBase] of DIRECTORY_MAP) {
    if (rel.startsWith(prefix)) {
      const rest = rel.slice(prefix.length);
      return uriBase + rest;
    }
  }
  return PROJECT_URI + rel;
}

function uriToPath(uri) {
  for (const [prefix, uriBase] of DIRECTORY_MAP) {
    if (uri.startsWith(uriBase)) {
      return PROJECT_ROOT + prefix + uri.slice(uriBase.length);
    }
  }
  if (uri.startsWith(PROJECT_URI)) {
    return PROJECT_ROOT + uri.slice(PROJECT_URI.length);
  }
  return null;
}

function parentUri(uri) {
  const parts = uri.replace(/\/$/, '').split('/');
  parts.pop();
  return parts.join('/') + '/';
}

// Extract flat list of MatchedContext from find/search response
function flattenResults(result) {
  const out = [];
  if (result.resources) out.push(...result.resources);
  if (result.skills) out.push(...result.skills);
  if (result.memories) out.push(...result.memories);
  return out;
}

// --- Event handlers ---

async function handleSessionStart() {
  const result = await contentOverview('viking://agent/skills/delve-subsystems');
  return { event: 'SessionStart', context: 'project_taxonomy', data: result };
}

async function handleUserPrompt(data) {
  const prompt = data.prompt || '';
  if (prompt.length < 10) return null;

  const raw = await find(prompt, 8, PROJECT_URI);
  const results = flattenResults(raw);
  if (results.length === 0) return null;

  const top2 = results.slice(0, 2);
  const rest = results.slice(2);

  const enriched = await Promise.all([
    ...top2.map(async (r) => {
      try {
        const ov = await contentOverview(r.uri);
        return { uri: r.uri, score: r.score, level: 'L1', content: ov };
      } catch {
        return { uri: r.uri, score: r.score, level: 'L0', content: r.abstract };
      }
    }),
    ...rest.map(async (r) => ({
      uri: r.uri,
      score: r.score,
      level: 'L0',
      content: r.abstract,
    })),
  ]);

  return { event: 'UserPromptSubmit', context: 'semantic_search', query: prompt, results: enriched };
}

async function handlePostRead(data) {
  const filePath = data.tool_input?.file_path;
  if (!filePath) return null;

  const uri = pathToUri(filePath);
  if (!uri) return null;

  const parentDir = parentUri(uri);
  const listing = await fsLs(parentDir);
  if (!listing || !Array.isArray(listing)) return null;

  // Filter to sibling directories (abstracts are directory-oriented) and files
  const fileUri = uri.replace(/\/$/, '');
  const siblings = listing.filter((item) => {
    const itemUri = (item.uri || '').replace(/\/$/, '');
    return itemUri !== fileUri;
  }).slice(0, 5);

  if (siblings.length === 0) return null;

  const abstracts = await Promise.all(
    siblings.map(async (item) => {
      try {
        const abs = await contentAbstract(item.uri);
        return { uri: item.uri, name: item.name, isDir: item.isDir, content: abs };
      } catch {
        return null;
      }
    })
  );

  const valid = abstracts.filter(Boolean);
  if (valid.length === 0) return null;

  return {
    event: 'PostToolUse_Read',
    context: 'sibling_files',
    file: filePath,
    siblings: valid,
  };
}

async function handlePostGrep(data) {
  const pattern = data.tool_input?.pattern;
  if (!pattern) return null;

  const raw = await find(pattern, 10, PROJECT_URI);
  const results = flattenResults(raw);
  if (results.length === 0) return null;

  // Deduplicate against files already in grep output
  const grepFiles = new Set();
  if (data.tool_output?.filePaths) {
    for (const fp of data.tool_output.filePaths) grepFiles.add(fp);
  }

  const remaining = results.filter((r) => {
    const path = uriToPath(r.uri);
    return !path || !grepFiles.has(path);
  });

  const top5 = remaining.slice(0, 5);
  const enriched = await Promise.all(
    top5.map(async (r, i) => {
      // Upgrade top result to L1 if high confidence
      if (i === 0 && r.score > 0.8) {
        try {
          const ov = await contentOverview(r.uri);
          return { uri: r.uri, score: r.score, level: 'L1', content: ov };
        } catch {
          return { uri: r.uri, score: r.score, level: 'L0', content: r.abstract };
        }
      }
      return { uri: r.uri, score: r.score, level: 'L0', content: r.abstract };
    })
  );

  return { event: 'PostToolUse_Grep', context: 'related_code', pattern, results: enriched };
}

// --- Output ---

function emit(hookEventName, contextObj) {
  const toonPayload = encode(contextObj);
  const response = {
    hookSpecificOutput: {
      ...(hookEventName !== 'PostToolUse' && { hookEventName }),
      additionalContext: `[OpenViking context — TOON-encoded]\n${toonPayload}`,
    },
  };
  console.log(JSON.stringify(response));
}

// --- Main ---

async function main() {
  let inputData = '';
  for await (const chunk of process.stdin) {
    inputData += chunk;
  }

  const data = JSON.parse(inputData);
  const event = data.hook_event_name;

  let result = null;

  if (event === 'SessionStart') {
    result = await handleSessionStart();
  } else if (event === 'UserPromptSubmit') {
    result = await handleUserPrompt(data);
  } else if (event === 'PostToolUse') {
    const tool = data.tool_name;
    if (tool === 'Read') {
      result = await handlePostRead(data);
    } else if (tool === 'Grep') {
      result = await handlePostGrep(data);
    }
  }

  if (result) {
    emit(event, result);
  }
}

main().catch(() => {
  // Graceful degradation: silent exit on any error
  process.exit(0);
});
