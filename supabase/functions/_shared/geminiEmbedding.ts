export const GEMINI_EMBEDDING_MODEL = "gemini-embedding-001";
export const GEMINI_EMBEDDING_DIMENSION = 1536;

export function embeddingApiKey(): string {
  return (
    Deno.env.get("GOOGLE_GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_AI_API_KEY")?.trim() ||
    Deno.env.get("GEMINI_API_KEY")?.trim() ||
    Deno.env.get("GOOGLE_CLOUD_API_KEY")?.trim() ||
    Deno.env.get("GCP_API_KEY")?.trim() ||
    ""
  );
}

async function embedGemini(
  apiKey: string,
  text: string,
  taskType: "RETRIEVAL_QUERY" | "RETRIEVAL_DOCUMENT",
): Promise<number[]> {
  if (!apiKey) throw new Error("Gemini embedding API key not configured");
  const model = `models/${GEMINI_EMBEDDING_MODEL}`;
  const url =
    `https://generativelanguage.googleapis.com/v1beta/${model}:embedContent?key=${
      encodeURIComponent(apiKey)
    }`;
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      model,
      content: { parts: [{ text }] },
      taskType,
      outputDimensionality: GEMINI_EMBEDDING_DIMENSION,
    }),
  });
  if (!res.ok) {
    const err = await res.text();
    throw new Error(`Gemini embeddings ${res.status}: ${err.slice(0, 300)}`);
  }
  const data = await res.json() as { embedding?: { values?: number[] } };
  const values = data.embedding?.values ?? [];
  if (values.length !== GEMINI_EMBEDDING_DIMENSION) {
    throw new Error(`Unexpected embedding dimension: ${values.length}`);
  }
  return values;
}

export async function embedRetrievalQuery(apiKey: string, text: string): Promise<number[]> {
  return await embedGemini(apiKey, text, "RETRIEVAL_QUERY");
}

export async function embedRetrievalDocument(apiKey: string, text: string): Promise<number[]> {
  return await embedGemini(apiKey, text, "RETRIEVAL_DOCUMENT");
}

export function vectorLiteral(values: number[]): string {
  return `[${values.join(",")}]`;
}
