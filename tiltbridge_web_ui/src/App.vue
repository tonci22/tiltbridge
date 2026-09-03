<template>
  <div>
    <TransitionRoot as="template" :show="sidebarOpen">
      <Dialog as="div" class="relative z-40 lg:hidden" @close="sidebarOpen = false">
        <TransitionChild as="template" enter="transition-opacity ease-linear duration-300" enter-from="opacity-0" enter-to="opacity-100" leave="transition-opacity ease-linear duration-300" leave-from="opacity-100" leave-to="opacity-0">
          <div class="fixed inset-0 bg-gray-600 bg-opacity-75" />
        </TransitionChild>

        <div class="fixed inset-0 z-40 flex">
          <TransitionChild as="template" enter="transition ease-in-out duration-300 transform" enter-from="-translate-x-full" enter-to="translate-x-0" leave="transition ease-in-out duration-300 transform" leave-from="translate-x-0" leave-to="-translate-x-full">
            <DialogPanel class="relative flex w-full max-w-xs flex-1 flex-col bg-indigo-700">
              <TransitionChild as="template" enter="ease-in-out duration-300" enter-from="opacity-0" enter-to="opacity-100" leave="ease-in-out duration-300" leave-from="opacity-100" leave-to="opacity-0">
                <div class="absolute top-0 right-0 -mr-12 pt-2">
                  <button type="button" class="ml-1 flex h-10 w-10 items-center justify-center rounded-full focus:outline-none focus:ring-2 focus:ring-inset focus:ring-white" @click="sidebarOpen = false">
                    <span class="sr-only">{{ $t("sitewide.close_sidebar") }}</span>
                    <XMarkIcon class="h-6 w-6 text-white" aria-hidden="true" />
                  </button>
                </div>
              </TransitionChild>
              <div class="h-0 flex-1 overflow-y-auto pt-5 pb-4">
                <div class="flex flex-shrink-0 items-center px-4">
                  <img class="h-8 w-auto" src="/logo.svg" alt="TiltBridge" />
                </div>
                <nav class="mt-5 px-2">
                  <!-- Mobile (small) navigation -->
                  <ul role="list" class="space-y-1">
                    <li v-for="item in navigation" :key="item.name">

                      <router-link v-if="!item.children" :to="{name: item.route_name}" v-slot="{ href, navigate, isActive }" custom>
                        <a :href="href" :class="[isActive ? activeItemClasses : inactiveItemClasses, navItemClasses]" @click="navigate($event); sidebarOpen = false">
                          <component :is="item.icon" class="h-6 w-6 flex-shrink-0 text-indigo-300" aria-hidden="true" />
                          {{ item.name }}
                        </a>
                      </router-link>

                      <!-- A group parent has to be a real button: it used to be an inert <span>
                           (reading an isActive that does not exist out here), so on a phone the
                           targets under it were unreachable. -->
                      <template v-else>
                        <button type="button"
                                :aria-expanded="isExpanded(item)"
                                :aria-controls="sectionPanelId(item)"
                                :class="[isSectionActive(item) ? activeItemClasses : inactiveItemClasses, navItemClasses, 'w-full text-left']"
                                @click="toggleSection(item)">
                          <component :is="item.icon" class="h-6 w-6 flex-shrink-0 text-indigo-300" aria-hidden="true" />
                          {{ item.name }}
                          <ChevronRightIcon :class="[isExpanded(item) ? 'rotate-90' : '', 'ml-auto h-5 w-5 flex-shrink-0 text-indigo-300 transition-transform']" aria-hidden="true" />
                        </button>
                        <ul :id="sectionPanelId(item)" class="mt-1 space-y-1" v-show="isExpanded(item)">
                          <li v-for="subItem in item.children" :key="subItem.name">
                            <router-link :to="{name: subItem.route_name}" v-slot="{ href, navigate, isActive }" custom>
                              <a :href="href" :class="[isActive ? activeItemClasses : inactiveItemClasses, subNavItemClasses, 'pl-11']" @click="navigate($event); sidebarOpen = false">
                                {{ subItem.name }}
                              </a>
                            </router-link>
                          </li>
                        </ul>
                      </template>

                    </li>
                  </ul>
                </nav>
              </div>

            </DialogPanel>
          </TransitionChild>
          <div class="w-14 flex-shrink-0" aria-hidden="true">
            <!-- Force sidebar to shrink to fit close icon -->
          </div>
        </div>
      </Dialog>
    </TransitionRoot>

    <!-- Static sidebar for desktop -->
    <!-- lg, not md: a 256px permanent sidebar on a 768-1023px screen is a phone held
         sideways losing a third of its width, which pushed gravity and temperature off the
         right edge of the Tilts table. Below lg the hamburger gives the page the full width. -->
    <div class="hidden lg:fixed lg:inset-y-0 lg:flex lg:w-64 lg:flex-col">
      <!-- Sidebar component, swap this element with another sidebar if you like -->
      <div class="flex min-h-0 flex-1 flex-col bg-indigo-700">
        <div class="flex flex-1 flex-col overflow-y-auto pt-5 pb-4">
          <div class="flex flex-shrink-0 items-center px-4">
            <img class="h-8 w-auto" src="/logo.svg" alt="TiltBridge" />
          </div>
          <nav class="mt-5 flex-1 space-y-1 px-2">
            <!-- Desktop (big) sidebar navigation -->
            <ul role="list" class="space-y-1">
              <li v-for="item in navigation" :key="item.name">

                <router-link v-if="!item.children" :to="{name: item.route_name}" v-slot="{ href, navigate, isActive }" custom>
                  <a :href="href" :class="[isActive ? activeItemClasses : inactiveItemClasses, deskItemClasses]" @click="navigate">
                    <component :is="item.icon" class="h-6 w-6 flex-shrink-0 text-indigo-300" aria-hidden="true" />
                    {{ item.name }}
                  </a>
                </router-link>

                <template v-else>
                  <button type="button"
                          :aria-expanded="isExpanded(item)"
                          :aria-controls="sectionPanelId(item, 'desktop')"
                          :class="[isSectionActive(item) ? activeItemClasses : inactiveItemClasses, deskItemClasses, 'w-full text-left leading-6']"
                          @click="toggleSection(item)">
                    <component :is="item.icon" class="h-6 w-6 flex-shrink-0 text-indigo-300" aria-hidden="true" />
                    {{ item.name }}
                    <ChevronRightIcon :class="[isExpanded(item) ? 'rotate-90' : '', 'ml-auto h-5 w-5 flex-shrink-0 text-indigo-300 transition-transform']" aria-hidden="true" />
                  </button>
                  <ul :id="sectionPanelId(item, 'desktop')" class="mt-1 space-y-1 px-2" v-show="isExpanded(item)">
                    <li v-for="subItem in item.children" :key="subItem.name">
                      <router-link :to="{name: subItem.route_name}" v-slot="{ href, navigate, isActive }" custom>
                        <a :href="href" :class="[isActive ? activeItemClasses : inactiveItemClasses, 'group flex items-center rounded-md px-4 py-2 text-sm font-medium']" @click="navigate">{{ subItem.name }}</a>
                      </router-link>
                    </li>
                  </ul>
                </template>

              </li>
            </ul>
          </nav>
        </div>
      </div>
    </div>



    <!-- End Collapsable Navbar -->
    <div class="flex flex-1 flex-col lg:pl-64">
      <!-- Mobile top bar. It carries the current page name because the sidebar is shut most of
           the time and a lone hamburger says nothing about where you are. -->
      <div class="sticky top-0 z-10 flex items-center gap-x-2 border-b border-gray-200 bg-white px-2 py-1.5 shadow-sm lg:hidden">
        <button type="button" class="inline-flex h-11 w-11 flex-shrink-0 items-center justify-center rounded-md text-gray-500 hover:bg-gray-100 hover:text-gray-900 focus:outline-none focus:ring-2 focus:ring-inset focus:ring-indigo-500" @click="sidebarOpen = true">
          <span class="sr-only">{{ $t("sitewide.open_sidebar") }}</span>
          <Bars3Icon class="h-6 w-6" aria-hidden="true" />
        </button>
        <span class="truncate text-base font-semibold text-gray-900">{{ currentPageTitle }}</span>
      </div>
      <main class="flex-1">
        <router-view class="us__content"  />
      </main>
    </div>
  </div>
</template>

<script setup>
import { ChevronRightIcon } from '@heroicons/vue/20/solid'

import { Dialog, DialogPanel, TransitionChild, TransitionRoot } from '@headlessui/vue'
import {
  Bars3Icon,
  HomeIcon,
  XMarkIcon,
  CloudArrowUpIcon,
  Cog8ToothIcon,
  QuestionMarkCircleIcon,
  InformationCircleIcon,
} from '@heroicons/vue/24/outline'

import { i18n } from "@/main.js";
import { cloudTargets } from "@/nav.js";
import { computed, ref } from "vue";
import { useRoute, useRouter } from "vue-router";

const sidebarOpen = ref(false);
const route = useRoute();
const router = useRouter();

const activeItemClasses = 'bg-indigo-800 text-white';
const inactiveItemClasses = 'text-white hover:bg-indigo-600 hover:bg-opacity-75';
/*
 * py-3/py-2.5 on the mobile rows rather than the desktop py-2: the smaller target leaves a
 * strip barely taller than the text, which is how a thumb ends up on the wrong entry.
 */
const navItemClasses = 'group flex items-center gap-x-3 rounded-md px-2 py-3 text-base font-medium';
const subNavItemClasses = 'group flex items-center rounded-md px-2 py-2.5 text-base font-medium';
const deskItemClasses = 'group flex items-center gap-x-3 rounded-md p-2 text-sm font-medium';

const navigation = [
  { name: i18n.global.t('sitewide.sidebar_options.tilts'), icon: HomeIcon, route_name: 'Home'},
  // section is the parent route: /config/queue/ is still "Configure", even though the sidebar
  // entry itself points at the general settings child.
  { name: i18n.global.t('sitewide.sidebar_options.configure'), icon: Cog8ToothIcon, route_name: 'TiltBridgeConfig', section: 'ConfigView'},
  { name: i18n.global.t('sitewide.sidebar_options.cloud_target'), icon: CloudArrowUpIcon, route_name: 'CloudConfigView', section: 'CloudConfigView', children: cloudTargets()},
  // { name: 'Calibration', icon: HomeIcon, route_name: 'Home'},
  { name: 'Help', icon: QuestionMarkCircleIcon, route_name: 'Help'},
  { name: 'About', icon: InformationCircleIcon, route_name: 'About'},
]

/* Names of every route in the current chain, so a child route lights up its parent section. */
const activeRouteNames = computed(() => route.matched.map((r) => r.name).filter(Boolean));

function isSectionActive(item) {
  const names = activeRouteNames.value;
  return names.includes(item.route_name)
      || (item.section && names.includes(item.section))
      || (item.children || []).some((child) => names.includes(child.route_name));
}

/*
 * Only sections the user has explicitly toggled are recorded; everything else falls back to
 * "expanded if we are inside it". Deliberately not headlessui's Disclosure defaultOpen, which
 * is read once - and on a cold load of /target/gsheets/ that read happens before the router
 * has resolved the initial navigation, so the group would come up collapsed.
 */
const sectionOverrides = ref({});

function isExpanded(item) {
  const override = sectionOverrides.value[item.name];
  return override === undefined ? isSectionActive(item) : override;
}

function toggleSection(item) {
  sectionOverrides.value = { ...sectionOverrides.value, [item.name]: !isExpanded(item) };
}

function sectionPanelId(item, prefix = 'mobile') {
  return `${prefix}-nav-${item.name.replace(/\W+/g, '-').toLowerCase()}`;
}

const currentPageTitle = computed(() => {
  for (const item of navigation) {
    if (!isSectionActive(item)) continue;
    // The specific target beats the group name: "Google Sheets" is what you navigated to.
    const child = (item.children || []).find((c) => activeRouteNames.value.includes(c.route_name));
    return child ? child.name : item.name;
  }
  return 'TiltBridge';
});

/*
 * The menu is a full-screen overlay on a phone, so leaving it up after a tap hides the page it
 * just opened. afterEach covers link taps, the back button and redirects alike; the handlers
 * on the links cover re-tapping the row you are already on, which navigates nowhere and so
 * never reaches this hook.
 */
router.afterEach(() => {
  sidebarOpen.value = false;
});
</script>



<style scoped>

</style>
