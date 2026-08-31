FROM node:22-alpine AS build
WORKDIR /workspace
RUN corepack enable
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml tsconfig.base.json tsconfig.json ./
COPY artifacts/trinity/package.json artifacts/trinity/package.json
COPY lib/api-client-react/package.json lib/api-client-react/package.json
COPY lib/api-spec/package.json lib/api-spec/package.json
COPY lib/api-zod/package.json lib/api-zod/package.json
COPY lib/db/package.json lib/db/package.json
COPY artifacts/mockup-sandbox/package.json artifacts/mockup-sandbox/package.json
COPY scripts/package.json scripts/package.json
RUN pnpm install --frozen-lockfile
COPY artifacts/trinity artifacts/trinity
COPY lib lib
COPY scripts scripts
ENV PORT=5173 BASE_PATH=/ NODE_ENV=production
RUN pnpm --filter @workspace/trinity run build

FROM nginx:1.31-alpine AS runtime
COPY deploy/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /workspace/artifacts/trinity/dist/public /usr/share/nginx/html
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
