FROM node:22-slim
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY server.js ./
COPY public ./public
ENV PORT=8080
ENV RELAY_STATE_PATH=/tmp/overlink-relay-grids.json
EXPOSE 8080
CMD ["node", "server.js"]
