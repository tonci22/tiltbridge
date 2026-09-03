<template>
  <header class="bg-white shadow">
    <div class="max-w-7xl mx-auto px-4 py-5 sm:px-6 sm:py-6 lg:px-8">
      <h1 class="text-2xl sm:text-3xl font-bold text-gray-900">
        <slot name="header"></slot>
      </h1>
    </div>
    <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 mt-1 sm:mt-4">
      <div class="sm:hidden pb-3">
        <MobileRouteSelect :tabs="props.tabs" />
      </div>
      <div class="hidden sm:block">
        <!-- overflow-x-auto so a long row of tabs scrolls instead of pushing the page sideways -->
        <nav class="-mb-px flex space-x-8 overflow-x-auto">
          <router-link v-for="tab in props.tabs" :key="tab.name" :to="{name: tab.route_name}" v-slot="{ href, navigate, isActive }" custom>
            <a :href="href" :class="[isActive ? 'border-indigo-500 text-indigo-600' : 'border-transparent text-gray-500 hover:text-gray-700 hover:border-gray-300', 'whitespace-nowrap pb-4 px-1 border-b-2 font-medium text-sm']" :aria-current="isActive ? 'page' : undefined" @click="navigate">
              {{ tab.name }}
            </a>
          </router-link>
        </nav>
      </div>
    </div>

  </header>

  <main>
    <router-view></router-view>
  </main>

</template>


<script setup>
import MobileRouteSelect from "@/components/sitewide/MobileRouteSelect.vue";

const props = defineProps({
  tabs: {
    type: Array,
    required: true
  }
});
</script>


<style scoped>

</style>
