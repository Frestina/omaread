// Draws TeX with MathJax, GitHub's math engine, for Omaread:
//
//   node -e "$(cat drawmath.cjs)" MATHJAX_DIR OUT_DIR SCALE < formulas.json
//
// formulas.json is [{key, tex, display, color}]. Each formula is written as
// OUT_DIR/key.svg, drawn in its colour at SCALE times its size so it stays
// sharp, or as OUT_DIR/key.failed when it cannot be drawn.
const fs = require('fs');
const path = require('path');

const [mathjaxDir, outDir, scaleArgument] = process.argv.slice(1);
const scale = Number(scaleArgument);
// Pixels in an ex of Noto Sans at 16px, the size Omaread's pages are
// measured at, so formulas match the text around them.
const exPixels = 8.6;

// MathJax tells the console about glyphs it cannot load; such a formula is
// marked as failed instead.
console.warn = console.error = () => {};

global.MathJax = {
  loader: { paths: { mathjax: mathjaxDir }, load: ['adaptors/liteDOM'], require: require },
  startup: { typeset: false },
  options: { enableEnrichment: false, enableSpeech: false, enableBraille: false },
  // One picture per formula: Qt cannot wrap a picture across lines anyway.
  svg: { fontCache: 'local', linebreaks: { inline: false } },
};
require(path.join(mathjaxDir, 'tex-svg.js'));

const formulas = JSON.parse(fs.readFileSync(0, 'utf8'));

function ex(value) {
  return parseFloat(value) || 0;
}

MathJax.startup.promise.then(async () => {
  const adaptor = MathJax.startup.adaptor;
  for (const formula of formulas) {
    const out = path.join(outDir, formula.key);
    try {
      const container = await MathJax.tex2svgPromise(formula.tex, { display: formula.display });
      const svg = adaptor.firstChild(container);
      let width = ex(adaptor.getAttribute(svg, 'width'));
      let height = ex(adaptor.getAttribute(svg, 'height'));
      const style = adaptor.getAttribute(svg, 'style') || '';
      const match = style.match(/vertical-align:\s*(-?[\d.]+)ex/);
      const depth = match ? -parseFloat(match[1]) : 0;
      let [minX, minY, boxWidth, boxHeight] = adaptor.getAttribute(svg, 'viewBox').split(/\s+/).map(Number);

      // Qt sets a picture in a line by its middle, at half an ex above the
      // baseline, so pad the formula until that is where its middle is.
      if (!formula.display) {
        const above = height - depth - 0.5;
        const below = depth + 0.5;
        const half = Math.max(above, below);
        const unitsPerEx = boxHeight / height;
        minY -= (half - above) * unitsPerEx;
        boxHeight += (2 * half - height) * unitsPerEx;
        height = 2 * half;
      }

      adaptor.setAttribute(svg, 'viewBox', [minX, minY, boxWidth, boxHeight].join(' '));
      adaptor.setAttribute(svg, 'width', `${width * exPixels * scale}px`);
      adaptor.setAttribute(svg, 'height', `${height * exPixels * scale}px`);
      adaptor.removeAttribute(svg, 'style');
      const text = adaptor.outerHTML(svg).replace(/currentColor/g, formula.color);
      fs.writeFileSync(out + '.svg', '<?xml version="1.0" encoding="UTF-8"?>\n' + text);
    } catch (error) {
      fs.writeFileSync(out + '.failed', String(error && error.message || error));
    }
  }
}).catch((error) => {
  process.stderr.write(`drawmath: ${error && error.stack || error}\n`);
  process.exit(1);
});
