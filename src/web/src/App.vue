<script setup>
import { elementLocale } from '@/i18n'
</script>

<template>
  <el-config-provider :locale="elementLocale">
    <router-view v-slot="{ Component }">
      <keep-alive include="MainLayout">
        <component :is="Component" />
      </keep-alive>
    </router-view>
  </el-config-provider>
</template>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: 'Plus Jakarta Sans', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
}
</style>
