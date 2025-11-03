import * as echarts from 'echarts';

const DEFAULT_THEME_NAME = 'tinybms-dark';
let themeRegistered = false;

const sharedTheme = {
  color: ['#00a896', '#ffd166', '#f25f5c', '#118ab2', '#073b4c'],
  backgroundColor: 'transparent',
  textStyle: {
    color: '#f2f5f7',
    fontFamily: '"Segoe UI", "Roboto", sans-serif',
  },
  title: {
    textStyle: {
      color: '#f2f5f7',
      fontWeight: 600,
    },
    subtextStyle: {
      color: '#b0c4d4',
    },
  },
  legend: {
    textStyle: {
      color: '#d9e2ec',
    },
  },
  tooltip: {
    backgroundColor: 'rgba(24, 38, 52, 0.95)',
    borderColor: 'rgba(0, 168, 150, 0.35)',
    textStyle: {
      color: '#f2f5f7',
    },
  },
  grid: {
    top: 48,
    left: 60,
    right: 32,
    bottom: 48,
  },
  xAxis: {
    axisLine: {
      lineStyle: {
        color: 'rgba(255, 255, 255, 0.15)',
      },
    },
    axisLabel: {
      color: '#d9e2ec',
    },
    splitLine: {
      lineStyle: {
        color: 'rgba(255, 255, 255, 0.08)',
      },
    },
  },
  yAxis: {
    axisLine: {
      lineStyle: {
        color: 'rgba(255, 255, 255, 0.15)',
      },
    },
    axisLabel: {
      color: '#d9e2ec',
    },
    splitLine: {
      lineStyle: {
        color: 'rgba(255, 255, 255, 0.08)',
      },
    },
  },
};

function ensureThemeRegistered() {
  if (!themeRegistered) {
    echarts.registerTheme(DEFAULT_THEME_NAME, sharedTheme);
    themeRegistered = true;
  }
}

export function initChart(domElement, options = {}, { theme = DEFAULT_THEME_NAME, renderer = 'canvas' } = {}) {
  if (!domElement) {
    throw new Error('Un élément DOM valide est requis pour initialiser un graphique.');
  }

  ensureThemeRegistered();
  const chart = echarts.init(domElement, theme, { renderer });

  if (options && Object.keys(options).length > 0) {
    chart.setOption(options);
  }

  const resizeObserver = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(() => chart.resize()) : null;
  const handleWindowResize = () => chart.resize();

  window.addEventListener('resize', handleWindowResize);
  if (resizeObserver) {
    resizeObserver.observe(domElement);
  }

  const dispose = () => {
    window.removeEventListener('resize', handleWindowResize);
    if (resizeObserver) {
      resizeObserver.disconnect();
    }
    if (!chart.isDisposed()) {
      chart.dispose();
    }
  };

  return { chart, dispose };
}

export function getSharedTheme() {
  ensureThemeRegistered();
  return { ...sharedTheme };
}

export { echarts, DEFAULT_THEME_NAME };
