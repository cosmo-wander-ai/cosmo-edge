import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import path from 'path'

export default defineConfig(({ mode }) => {
  // 加载环境变量
  const env = loadEnv(mode, process.cwd())

  return {
    // 设置基础路径
    base: env.VITE_APP_BASE_URL || '/',
    plugins: [vue()],
    resolve: {
      alias: {
        '@': path.resolve(__dirname, 'src')
      }
    },
    css: {
      preprocessorOptions: {
        scss: {
          api: 'modern-compiler'
        }
      }
    },
    build: {
      chunkSizeWarningLimit: 1500,
      rollupOptions: {
        output: {
          manualChunks(id) {
            if (id.includes('node_modules')) {
              if (id.includes('element-plus')) return 'vendor-element'
              if (id.includes('/vue')) return 'vendor-vue'
              if (id.includes('echarts')) return 'vendor-echarts'
              if (id.includes('@vue-flow')) return 'vendor-vue-flow'
              if (id.includes('@antv/x6') || id.includes('@antv/layout')) return 'vendor-graph'
              // dagre and graphlib both depend on lodash; keep all three in the same chunk to avoid circular chunk references
              if (id.includes('lodash') || id.includes('dagre') || id.includes('graphlib')) return 'vendor-lodash'
              if (id.includes('moment')) return 'vendor-moment'
              if (id.includes('highlight.js') || id.includes('markdown-it')) return 'vendor-md'
              return 'vendor'
            }
          }
        }
      }
    },
    server: {
      port: 3000,
      proxy: {
        '/gtw': {
          target: env.VITE_APP_API_URL,
          changeOrigin: true,
          secure: false,
          rewrite: (path) => path
        },
        '/event': {
          target: env.VITE_APP_API_URL,
          changeOrigin: true,
          secure: false,
          rewrite: (path) => path
        },
        '/weblogo': {
          target: env.VITE_APP_API_URL,
          changeOrigin: true,
          secure: false,
          rewrite: (path) => path
        },
        '/web': {
          target: env.VITE_APP_API_URL,
          changeOrigin: true,
          secure: false,
          rewrite: (path) => path
        }
      }
    }
  }
})
