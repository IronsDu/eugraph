// Integration tests for EuGraph Bolt WebSocket transport.
//
// The Neo4j JavaScript driver's browser bundle uses WebSocket when loaded into
// Node with a WebSocket implementation available.  This test intentionally does
// not use the regular ``neo4j-driver`` entrypoint, because that entrypoint uses
// raw TCP in Node.js.

'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { test, before, after } = require('node:test');

const driverRoot = process.env.EUGRAPH_NODE_DRIVER_PATH;
if (!driverRoot) {
  throw new Error('EUGRAPH_NODE_DRIVER_PATH must point to an npm prefix');
}

// The browser bundle needs a WHATWG-style WebSocket implementation.  Node 22
// provides one, but ``ws`` is installed explicitly so the tests work on older
// Node versions too.
const wsPath = path.join(driverRoot, 'node_modules', 'ws');
globalThis.WebSocket = require(wsPath);

const neo4j = require(path.join(
  driverRoot,
  'node_modules',
  'neo4j-driver',
  'lib',
  'browser',
  'neo4j-web.min.js'
));

const BOLT_PORT = process.env.EUGRAPH_BOLT_PORT || '17687';
const TEST_DATABASE = 'bolt_test';

let adminDriver;

function createDriver() {
  return neo4j.driver(
    `bolt://localhost:${BOLT_PORT}`,
    neo4j.auth.basic('neo4j', 'eugraph'),
    {
      encrypted: false,
      disableLosslessIntegers: true,
    }
  );
}

async function run(queries) {
  const driver = createDriver();
  const session = driver.session({ database: TEST_DATABASE });
  try {
    return await queries(session);
  } finally {
    await session.close();
    await driver.close();
  }
}

before(async () => {
  adminDriver = createDriver();
  const session = adminDriver.session();
  try {
    await session.run(`DROP DATABASE ${TEST_DATABASE}`).catch(() => {});
    await session.run(`CREATE DATABASE ${TEST_DATABASE}`);
  } finally {
    await session.close();
  }
});

after(async () => {
  if (!adminDriver) {
    return;
  }

  const session = adminDriver.session();
  try {
    await session.run(`DROP DATABASE ${TEST_DATABASE}`).catch(() => {});
  } finally {
    await session.close();
    await adminDriver.close();
  }
});

test('verify connectivity over WebSocket', async () => {
  const driver = createDriver();
  try {
    await driver.verifyConnectivity();
  } finally {
    await driver.close();
  }
});

test('RETURN scalar over WebSocket', async () => {
  const records = await run((session) =>
    session.run('RETURN 1 AS n').then((result) => result.records)
  );

  assert.equal(records.length, 1);
  assert.equal(records[0].get('n'), 1);
});

test('create and match a node', async () => {
  await run((session) =>
    session.run('CREATE (n:Person {name: $name, age: $age})', {
      name: 'Alice',
      age: 30,
    })
  );

  const records = await run((session) =>
    session
      .run('MATCH (n:Person {name: $name}) RETURN n.name, n.age', {
        name: 'Alice',
      })
      .then((result) => result.records)
  );

  assert.equal(records.length, 1);
  assert.equal(records[0].get('n.name'), 'Alice');
  assert.equal(records[0].get('n.age'), 30);
});

test('create and match an edge', async () => {
  await run((session) =>
    session.run(
      'CREATE (a:Person {name: $a_name}) ' +
        'CREATE (b:Person {name: $b_name}) ' +
        'CREATE (a)-[:KNOWS {since: 2020}]->(b)',
      { a_name: 'Alice', b_name: 'Bob' }
    )
  );

  const records = await run((session) =>
    session
      .run(
        'MATCH (a:Person {name: $a_name})-[r:KNOWS]->' +
          '(b:Person {name: $b_name}) ' +
          'RETURN a.name, b.name, r.since',
        { a_name: 'Alice', b_name: 'Bob' }
      )
      .then((result) => result.records)
  );

  assert.equal(records.length, 1);
  assert.equal(records[0].get('a.name'), 'Alice');
  assert.equal(records[0].get('b.name'), 'Bob');
  assert.equal(records[0].get('r.since'), 2020);
});

test('round-trip common scalar and container types', async () => {
  const records = await run((session) =>
    session
      .run(
        'RETURN null AS n, true AS b, 3.14 AS f, ' +
          "'hello' AS s, [1, 2, 3] AS list, " +
          '{name: "Alice", age: 30} AS map'
      )
      .then((result) => result.records)
  );

  const record = records[0];
  assert.equal(record.get('n'), null);
  assert.equal(record.get('b'), true);
  assert.equal(record.get('f'), 3.14);
  assert.equal(record.get('s'), 'hello');
  assert.deepEqual(record.get('list'), [1, 2, 3]);
  assert.deepEqual(record.get('map'), { name: 'Alice', age: 30 });
});

test('round-trip Cypher temporal values', async () => {
  const records = await run((session) =>
    session
      .run(
        "RETURN date('2025-01-15') AS d, " +
          "datetime('2025-01-15T10:30:00+08:00') AS dt, " +
          "time('10:30:00') AS t"
      )
      .then((result) => result.records)
  );

  const record = records[0];
  assert.equal(neo4j.isDate(record.get('d')), true);
  assert.equal(neo4j.isDateTime(record.get('dt')), true);
  assert.equal(neo4j.isTime(record.get('t')), true);
});

test('pass typed parameters', async () => {
  const records = await run((session) =>
    session
      .run(
        'RETURN $name AS name, $n AS n, $x AS x, $items AS items',
        {
          name: 'Alice',
          n: neo4j.int(42),
          x: 3.14,
          items: [1, 2, 3],
        }
      )
      .then((result) => result.records)
  );

  const record = records[0];
  assert.equal(record.get('name'), 'Alice');
  assert.equal(record.get('n'), 42);
  assert.equal(record.get('x'), 3.14);
  assert.deepEqual(record.get('items'), [1, 2, 3]);
});

test('explicit transaction commit', async () => {
  await run(async (session) => {
    const tx = session.beginTransaction();
    try {
      await tx.run('CREATE (n:TxTest {val: 1})');
      await tx.commit();
    } finally {
      await tx.close();
    }
  });

  const records = await run((session) =>
    session
      .run('MATCH (n:TxTest {val: 1}) RETURN n.val')
      .then((result) => result.records)
  );

  assert.equal(records.length, 1);
  assert.equal(records[0].get('n.val'), 1);
});

test('explicit transaction rollback', async () => {
  await run(async (session) => {
    const tx = session.beginTransaction();
    try {
      await tx.run('CREATE (n:TxTest {val: 999})');
      await tx.rollback();
    } finally {
      await tx.close();
    }
  });

  const records = await run((session) =>
    session
      .run('MATCH (n:TxTest {val: 999}) RETURN count(n) AS cnt')
      .then((result) => result.records)
  );

  assert.equal(records[0].get('cnt'), 0);
});
