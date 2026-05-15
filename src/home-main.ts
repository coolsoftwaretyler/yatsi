import { el, siteFooter } from './ui/components';

function init(): void {
  const root = document.getElementById('app')!;

  const header = el('header', { className: 'home-header' },
    el('h1', {}, 'YATSI'),
    el('p', { className: 'home-tagline' }, 'Yet Another TypeScript Implementation'),
  );

  const intro = el('p', { className: 'home-intro' },
    'A from-scratch TypeScript compiler and runtime, tested against the ECMAScript specification.',
  );

  const cards = el('div', { className: 'home-cards' },
    el('a', { className: 'home-card', href: './test262/' },
      el('h2', {}, 'Test262 Runner'),
      el('p', {}, 'Run the official ECMAScript conformance test suite against the Yatsi engine.'),
    ),
    el('a', { className: 'home-card', href: './custom/' },
      el('h2', {}, 'Custom Tests'),
      el('p', {}, 'Run hand-written tests for language features under active development.'),
    ),
    el('a', { className: 'home-card', href: './playground/' },
      el('h2', {}, 'Playground'),
      el('p', {}, 'Interactive editor with step-through debugging for the lexer and parser.'),
    ),
    el('a', { className: 'home-card', href: './control-flow-compilation/' },
      el('h2', {}, 'Control Flow'),
      el('p', {}, 'Visualize how if/else, while, and for loops compile to bytecode with jump patching.'),
    ),
    el('a', { className: 'home-card', href: './closures/' },
      el('h2', {}, 'Closures & Upvalues'),
      el('p', {}, 'Step through how closures capture variables via upvalues across nested function scopes.'),
    ),
  );

  root.appendChild(header);
  root.appendChild(intro);
  root.appendChild(cards);
  root.appendChild(siteFooter());
}

init();
