<template>
  <header class="bg-white shadow">
    <div class="max-w-7xl mx-auto px-4 py-5 sm:px-6 sm:py-6 lg:px-8">
      <h1 class="text-2xl sm:text-3xl font-bold text-gray-900">
        <slot name="header"></slot>
      </h1>
    </div>
    <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 mt-1 sm:mt-4">
      <!-- No tab row on wide screens - the sidebar already lists these. On a phone the sidebar
           is hidden behind the hamburger, so without this picker there is no way to move
           between siblings without going back out to the menu. -->
      <div class="sm:hidden pb-3" v-if="props.tabs.length > 0">
        <MobileRouteSelect :tabs="props.tabs" />
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
    required: false,
    default: () => [],
  }
});
</script>


<style scoped>

</style>
