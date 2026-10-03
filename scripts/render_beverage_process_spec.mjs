// Optional Excel renderer. Verification needs only Python's standard library.
// Usage: node render_beverage_process_spec.mjs [repository-root] [preview-dir]
// Requires @oai/artifact-tool in the authoring environment.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const root = path.resolve(process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), '..'));
const dir = path.join(root, 'examples/beverage_nl_extension/process_spec');
const previewDir = path.resolve(process.argv[3] ?? path.join(root, 'build/process-spec-preview'));
await fs.mkdir(previewDir, { recursive: true });
const input = JSON.parse(await fs.readFile(path.join(dir, 'workbook_data.json'), 'utf8'));
const wb = Workbook.create();
const column = n => {
  let result = '';
  for (let k = n + 1; k > 0; k = Math.floor((k - 1) / 26)) result = String.fromCharCode(65 + (k - 1) % 26) + result;
  return result;
};
const widthUnits = s => [...String(s ?? '')].reduce((n, c) => n + (c.charCodeAt(0) > 255 ? 1.8 : 1), 0);
const log = [];

for (const [index, data] of input.sheets.entries()) {
  const sh = wb.worksheets.add(data.name);
  const last = column(data.headers.length - 1);
  const end = data.rows.length + 5;
  const widths = data.headers.map((_, c) => data.widths?.[c] ?? 180);
  sh.showGridLines = false;
  if (index === 0) sh.tabColor = '#203E60';
  sh.getRange(`A1:${last}${end}`).format.font = { name: 'Arial', size: 10, color: '#243447' };
  sh.getRange(`A1:${last}${end}`).format.verticalAlignment = 'center';
  sh.getRange(`A1:${last}${end}`).format.rowHeightPx = 28;
  sh.getRange('A2').values = [[data.title]];
  sh.getRange('A2').format.font = { name: 'Arial', size: 15, bold: true, color: '#203E60' };
  sh.getRange(`A2:${last}2`).format.rowHeightPx = 34;
  sh.getRange(`A2:${last}2`).format.borders = { bottom: { style: 'thin', color: '#8FA1B4' } };
  sh.getRange('A3').values = [[data.note]];
  sh.getRange('A3').format.font = { name: 'Arial', size: 10, italic: true, color: '#596879' };
  sh.getRange(`A5:${last}5`).values = [data.headers];
  sh.getRange(`A5:${last}5`).format.fill = '#203E60';
  sh.getRange(`A5:${last}5`).format.font = { name: 'Arial', size: 10, bold: true, color: '#FFFFFF' };
  sh.getRange(`A5:${last}5`).format.horizontalAlignment = 'center';
  sh.getRange(`A5:${last}5`).format.wrapText = true;
  sh.getRange(`A5:${last}5`).format.rowHeightPx = 42;
  sh.getRange(`A5:${last}5`).format.borders = { insideVertical: { style: 'thin', color: '#FFFFFF' } };
  sh.getRange(`A6:${last}${end}`).values = data.rows;
  sh.getRange(`A6:${last}${end}`).format.wrapText = true;
  sh.getRange(`A6:${last}${end}`).format.verticalAlignment = 'top';
  for (let c = 0; c < data.headers.length; c++) {
    sh.getRange(`${column(c)}1:${column(c)}${end}`).format.columnWidthPx = widths[c];
    if (data.rows.every(row => row[c] === null || typeof row[c] === 'number')) {
      sh.getRange(`${column(c)}6:${column(c)}${end}`).setNumberFormat('0');
      sh.getRange(`${column(c)}6:${column(c)}${end}`).format.horizontalAlignment = 'right';
    }
  }
  for (const [r, row] of data.rows.entries()) {
    const lines = Math.max(...row.map((v, c) => String(v ?? '').split('\n').reduce((n, line) => n + Math.max(1, Math.ceil(widthUnits(line) * 6.6 / Math.max(40, widths[c] - 18))), 0)));
    sh.getRange(`A${r + 6}:${last}${r + 6}`).format.rowHeightPx = Math.max(30, lines * 17 + 12);
    if (r % 2 === 1) sh.getRange(`A${r + 6}:${last}${r + 6}`).format.fill = '#F1F4F7';
  }
  if (end > 25 || widths.reduce((a, b) => a + b, 0) > 1400) {
    sh.freezePanes.freezeRows(5);
    sh.freezePanes.freezeColumns(1);
  }
  const table = sh.tables.add(`A5:${last}${end}`, true, `ProcessTable${index + 1}`);
  table.showFilterButton = true;
  if (data.name === '配方参数') {
    sh.getRange(`F6:F${end}`).formulas = data.rows.map((_, r) => [`=ROUNDUP(1000*D${r + 6}/E${r + 6},0)`]);
    sh.getRange(`I6:I${end}`).formulas = data.rows.map((_, r) => [`=G${r + 6}*H${r + 6}`]);
    sh.getRange(`J6:J${end}`).formulas = data.rows.map((_, r) => [`=D${r + 6}*I${r + 6}`]);
  }
  if (data.name === '增量对比') {
    sh.getRange(`D6:D${end}`).formulas = data.rows.map((_, r) => [`=1-C${r + 6}/B${r + 6}`]);
    sh.getRange(`D6:D${end}`).setNumberFormat('0.00%');
  }
  log.push({ sheet: data.name, rows: data.rows.length, columns: data.headers.length });
}
await wb.recalculate();
for (const data of input.sheets) {
  const last = column(data.headers.length - 1);
  const end = data.rows.length + 5;
  // First screen on every worksheet; additional samples from the long action table.
  const windows = [{ first: 1, last: Math.min(end, 17), name: 'head' }];
  if (data.rows.length > 100) {
    const middle = Math.floor(end / 2);
    windows.push({ first: middle, last: middle + 5, name: 'middle' }, { first: end - 5, last: end, name: 'tail' });
  }
  for (const window of windows) {
    const png = await wb.render({ sheetName: data.name, range: `A${window.first}:${last}${window.last}`, scale: 1, format: 'png' });
    await fs.writeFile(path.join(previewDir, `${data.name}-${window.name}.png`), new Uint8Array(await png.arrayBuffer()));
  }
}
const formulas = await wb.inspect({ kind: 'region', sheetId: '配方参数', range: 'A6:J14', maxChars: 3500, tableMaxRows: 9, tableMaxCols: 10 });
const errors = await wb.inspect({ kind: 'match', searchTerm: '#REF!|#VALUE!|#DIV/0!|#NAME\\?|#NUM!|#N/A', options: { useRegex: true, maxResults: 20 }, maxChars: 2000 });
await fs.writeFile(path.join(previewDir, 'qa.json'), JSON.stringify({ sheets: log, formulas: formulas.ndjson, errors: errors.ndjson }, null, 2));
const output = await SpreadsheetFile.exportXlsx(wb);
await output.save(path.join(dir, 'process_spec.xlsx'));
// Keep the authoring engine's optional diagnostic dump with QA previews.
const inspection = path.join(dir, 'process_spec.xlsx.inspect.ndjson');
try {
  await fs.access(inspection);
  await fs.rename(inspection, path.join(previewDir, 'export-inspection.ndjson'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
console.log(JSON.stringify({ output: path.join(dir, 'process_spec.xlsx'), sheets: log, errors: errors.ndjson }));
