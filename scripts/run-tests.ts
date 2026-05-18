import { readdirSync, readFileSync, existsSync } from "node:fs";
import { join, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";

// Colors for terminal output
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const DIM = "\x1b[2m";
const BOLD = "\x1b[1m";
const RESET = "\x1b[0m";

interface TestCase {
  name: string; // e.g. "basic/hello"
  sourcePath: string;
  expectedPath: string;
}

function discoverTests(testsDir: string): TestCase[] {
  const tests: TestCase[] = [];

  for (const category of readdirSync(testsDir, { withFileTypes: true })) {
    if (!category.isDirectory()) continue;
    const categoryDir = join(testsDir, category.name);

    for (const file of readdirSync(categoryDir, { withFileTypes: true })) {
      if (!file.isFile() || !file.name.endsWith(".ts")) continue;
      const testName = basename(file.name, ".ts");
      const sourcePath = join(categoryDir, file.name);
      const expectedPath = join(categoryDir, `${testName}.expected`);

      if (existsSync(expectedPath)) {
        tests.push({
          name: `${category.name}/${testName}`,
          sourcePath,
          expectedPath,
        });
      }
    }
  }

  return tests.sort((a, b) => a.name.localeCompare(b.name));
}

function main() {
  const scriptDir = fileURLToPath(new URL(".", import.meta.url));
  const rootDir = join(scriptDir, "..");
  const testsDir = join(rootDir, "src", "custom", "tests");
  const cliBin = join(rootDir, "engine", "build-cli", "yatsi_cli");

  if (!existsSync(cliBin)) {
    console.error(
      `CLI binary not found at ${cliBin}\nRun ./build-cli.sh first.`
    );
    process.exit(1);
  }

  const tests = discoverTests(testsDir);
  if (tests.length === 0) {
    console.error("No tests found in", testsDir);
    process.exit(1);
  }

  console.log(`\n${BOLD}Running ${tests.length} tests...${RESET}\n`);

  let passed = 0;
  let failed = 0;
  const failures: {
    name: string;
    expected: string;
    actual: string;
    stderr: string;
  }[] = [];

  for (const test of tests) {
    const expected = readFileSync(test.expectedPath, "utf-8").trim();

    // Build CLI args based on test category
    const args: string[] = [];
    if (test.name.startsWith("typecheck/")) {
      args.push("--typecheck");
    } else if (test.name.startsWith("dump-types/")) {
      args.push("--dump-types");
    }
    args.push(test.sourcePath);

    let actual: string;
    let stderr = "";
    try {
      actual = execFileSync(cliBin, args, {
        encoding: "utf-8",
        timeout: 10_000,
      }).trim();
    } catch (err: any) {
      // execFileSync throws on non-zero exit code
      actual = (err.stdout ?? "").trim();
      stderr = (err.stderr ?? "").trim();
    }

    if (actual === expected) {
      console.log(`  ${GREEN}PASS${RESET} ${DIM}${test.name}${RESET}`);
      passed++;
    } else {
      console.log(`  ${RED}FAIL${RESET} ${test.name}`);
      failures.push({ name: test.name, expected, actual, stderr });
      failed++;
    }
  }

  // Summary
  console.log(
    `\n${BOLD}Results: ${passed} passed, ${failed} failed, ${tests.length} total${RESET}\n`
  );

  // Failure details
  if (failures.length > 0) {
    console.log(`${RED}${BOLD}Failures:${RESET}\n`);
    for (const f of failures) {
      console.log(`  ${BOLD}${f.name}${RESET}`);
      console.log(`    ${DIM}expected:${RESET} ${JSON.stringify(f.expected)}`);
      console.log(`    ${DIM}actual:${RESET}   ${JSON.stringify(f.actual)}`);
      if (f.stderr) {
        console.log(`    ${DIM}stderr:${RESET}   ${f.stderr}`);
      }
      console.log();
    }
    process.exit(1);
  }
}

main();
