<template>
  <header class="bg-white shadow">
    <div class="max-w-7xl mx-auto py-6 px-4 sm:px-6 lg:px-8">
      <h1 class="text-3xl font-bold text-gray-900">
        <slot name="header"></slot>
      </h1>
    </div>
    <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 mt-3 sm:mt-4">
      <div class="sm:hidden pb-2">
        <label for="current-tab" class="sr-only">{{ $t('sitewide.tab_container_sr_text') }}</label>
        <select id="current-tab" name="current-tab" class="block w-full pl-3 pr-10 py-2 text-base border-gray-300 focus:outline-none focus:ring-indigo-500 focus:border-indigo-500 sm:text-sm rounded-md" @change="routeChange">
          <!--          <option v-for="tab in tabs" :key="tab.name" :selected="isCurTab(tab.name)">{{ tab.name }}</option>-->
          <router-link v-for="tab in props.tabs" :key="tab.name" :to="{name: tab.route_name}" v-slot="{ href, isActive }" custom>
            <option :value="href" :selected="isActive">{{ tab.name }}</option>
            <!--            <a :href="href" :class="[isActive ? 'border-indigo-500 text-indigo-600' : 'border-transparent text-gray-500 hover:text-gray-700 hover:border-gray-300', 'whitespace-nowrap pb-4 px-1 border-b-2 font-medium text-sm']" :aria-current="isActive ? 'page' : undefined" @click="navigate">-->
            <!--              {{ tab.name }}-->
            <!--            </a>-->
          </router-link>


        </select>
      </div>
      <div class="hidden sm:block">
        <nav class="-mb-px flex space-x-8">
          <!--          <a v-for="tab in tabs" :key="tab.name" :href="tab.href" :class="[isCurTab(tab.name) ? 'border-indigo-500 text-indigo-600' : 'border-transparent text-gray-500 hover:text-gray-700 hover:border-gray-300', 'whitespace-nowrap pb-4 px-1 border-b-2 font-medium text-sm']" :aria-current="isCurTab(tab.name) ? 'page' : undefined">-->
          <!--            {{ tab.name }}-->
          <!--          </a>-->

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
import { defineProps } from "vue";

// const tabs = [
//   { name: 'General Settings', route_name: 'TiltBridgeConfig' },
// ];

const props = defineProps({
  tabs: {
    type: Object,
    required: true
  }
});

function routeChange(e) {
  this.$router.push(e.target.value)
}


</script>


<style scoped>

</style>