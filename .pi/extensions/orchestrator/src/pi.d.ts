// Ambient type declarations for pi runtime modules
declare module "@mariozechner/pi-ai" {
  export const Type: {
    Object: (schema: Record<string, any>) => any;
    String: (opts?: Record<string, any>) => any;
    Array: (items: any, opts?: Record<string, any>) => any;
    Boolean: (opts?: Record<string, any>) => any;
    Number: (opts?: Record<string, any>) => any;
  };
}

declare module "@mariozechner/pi-coding-agent" {
  export interface ExtensionAPI {
    registerTool(tool: {
      name: string;
      label: string;
      description: string;
      parameters: any;
      execute: (
        toolCallId: any,
        params: any,
        signal: any,
        onUpdate: any,
        ctx: any,
      ) => Promise<{ content: { type: string; text: string }[] }>;
    }): void;
    registerCommand(
      name: string,
      opts: {
        description: string;
        parameters?: any;
        handler: (args: any, ctx: any) => Promise<any>;
      },
    ): void;
    on(
      event: string,
      handler: (event: any, ctx: any) => Promise<any>,
    ): void;
  }
}
