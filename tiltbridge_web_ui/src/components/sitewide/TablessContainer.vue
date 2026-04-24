<template>
  <header class="bg-white shadow">
    <div class="max-w-7xl mx-auto py-6 px-4 sm:px-6 lg:px-8">
      <h1 class="text-3xl font-bold text-gray-900">
        <slot name="header"></slot>
      </h1>
    </div>
    <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 mt-3 sm:mt-4">
    </div>

  </header>

  <main>
    <router-view></router-view>
  </main>

</template>


<script setup>

</script>


<style scoped>

</style>