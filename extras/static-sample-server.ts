import { serve } from "https://deno.land/std/http/mod.ts";
import * as path from "https://deno.land/std/path/mod.ts";

const dir = path.resolve(path.dirname(path.fromFileUrl(import.meta.url)));

const reqHandler = async (req: Request) => {
  const urlPath = new URL(req.url).pathname;
  let filePath: string;
  switch (urlPath) {
    case "":
    case "/":
    case "/index.htm":
    case "/index.html":
      filePath = path.join(dir, "sample.html");
      break;
    default:
      if (urlPath.startsWith("/static/")) {
        filePath = path.join(dir, "..", urlPath);
      } else {
        return new Response("what you say?", { status: 404 });
      }
  }
  try {
    const body = (await Deno.open(filePath)).readable;
    return new Response(body);
  } catch (e) {
    if (e instanceof Deno.errors.NotFound) {
      return new Response("what you say?", { status: 404 });
    }
    return new Response(e.toString(), { status: 500 });
  }
};

serve(reqHandler, { port: 8080 });
