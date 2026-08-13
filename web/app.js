(function () {
  const $ = (id) => document.getElementById(id);

  let driver = null;

  function setStatus(text, isError) {
    const el = $("status");
    el.textContent = text;
    el.style.color = isError ? "#b42318" : "#445";
  }

  function buildUrl() {
    const host = $("host").value.trim() || "localhost";
    const port = $("port").value.trim() || "7687";
    return `bolt://${host}:${port}`;
  }

  function databaseName() {
    const value = $("database").value.trim();
    return value || "default";
  }

  function closeDriver() {
    if (driver) {
      driver.close().catch(() => {});
      driver = null;
    }
    $("runBtn").disabled = true;
  }

  async function connect() {
    closeDriver();

    const url = buildUrl();
    const username = $("username").value.trim() || "neo4j";
    const password = $("password").value;

    setStatus(`正在连接 ${url} ...`);
    try {
      driver = neo4j.driver(url, neo4j.auth.basic(username, password), {
        connectionTimeout: 5000,
      });
      await driver.verifyConnectivity({ database: databaseName() });
      $("runBtn").disabled = false;
      setStatus(`已连接：${url} / ${databaseName()}`);
    } catch (err) {
      closeDriver();
      setStatus(`连接失败：${err.message || err}`, true);
    }
  }

  async function executeQuery() {
    if (!driver) {
      setStatus("请先连接", true);
      return;
    }

    const query = $("query").value.trim();
    if (!query) {
      setStatus("请输入 Cypher 命令", true);
      return;
    }

    const session = driver.session({
      database: databaseName(),
      defaultAccessMode: neo4j.session.WRITE,
    });

    setStatus("执行中...");
    $("output").textContent = "";

    try {
      const result = await session.run(query);
      const rows = result.records.map((record) => {
        const obj = record.toObject();
        return JSON.stringify(obj, null, 2);
      });

      $("output").textContent = rows.length
        ? rows.join("\n---\n")
        : "(查询成功，0 行结果)";
      setStatus(`执行成功，${result.records.length} 行`);
    } catch (err) {
      $("output").textContent = err.message || String(err);
      setStatus("执行失败", true);
    } finally {
      await session.close();
    }
  }

  $("connectBtn").addEventListener("click", connect);
  $("disconnectBtn").addEventListener("click", () => {
    closeDriver();
    setStatus("未连接");
  });
  $("runBtn").addEventListener("click", executeQuery);
})();
