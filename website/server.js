const express = require("express");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 8080;
const root = path.join(__dirname, "public");

app.use((req, res, next) => {
  res.setHeader("X-Content-Type-Options", "nosniff");
  res.setHeader("Referrer-Policy", "strict-origin-when-cross-origin");
  next();
});

app.get("/healthz", (_req, res) => res.json({ ok: true, product: "Overlink" }));

app.use(express.static(root, { maxAge: "1h", extensions: ["html"] }));

app.use((req, res) => {
  if (req.accepts("html")) {
    res.status(404).sendFile(path.join(root, "index.html"));
    return;
  }
  res.status(404).json({ ok: false, error: "not found" });
});

app.listen(PORT, () => {
  console.log(`[overlink-web] listening on :${PORT}`);
});
