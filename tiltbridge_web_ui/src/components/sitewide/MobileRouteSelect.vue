<template>
  <div>
    <label :for="uid" class="sr-only">{{ $t('sitewide.tab_container_sr_text') }}</label>
    <!-- text-base, not sm:text-sm: anything under 16px makes iOS Safari zoom the whole page
         in when the select is tapped, and it never zooms back out. -->
    <select
        :id="uid"
        :value="currentHref"
        class="block w-full rounded-md border-gray-300 py-2.5 pl-3 pr-10 text-base focus:border-indigo-500 focus:outline-none focus:ring-indigo-500"
        @change="onChange">
      <option v-for="tab in tabs" :key="tab.route_name" :value="hrefFor(tab)">{{ tab.name }}</option>
    </select>
  </div>
</template>


<script setup>
/*
 * The small-screen stand-in for a row of tabs.
 *
 * Deliberately a plain <select> driven by a change handler rather than a list of <router-link>
 * elements rendering <option>s: an <option> is not clickable, so the links never fired and
 * picking an entry did nothing at all.
 */
import { computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';

const props = defineProps({
  tabs: {
    type: Array,
    required: true,
  },
});

const route = useRoute();
const router = useRouter();

const uid = `route-select-${Math.random().toString(36).slice(2, 10)}`;

function hrefFor(tab) {
  return router.resolve({ name: tab.route_name }).href;
}

/*
 * Tracks the route, not the last change event - the sidebar and the desktop tabs both navigate
 * without touching this select, and it has to still show where we actually are.
 */
const currentHref = computed(() => {
  const activeNames = route.matched.map((r) => r.name);
  const active = props.tabs.find((tab) => activeNames.includes(tab.route_name));
  return active ? hrefFor(active) : '';
});

function onChange(event) {
  router.push(event.target.value);
}
</script>


<style scoped>

</style>
