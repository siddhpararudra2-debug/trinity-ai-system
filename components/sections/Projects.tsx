import SectionHeading from '@/components/ui/SectionHeading';
import Card from '@/components/ui/Card';

const projects = [
  {
    title: 'Automated Assembly Line — Tata Motors',
    description: 'Designed and deployed a fully automated assembly line reducing cycle time by 40% and improving throughput by 3x for automotive component manufacturing.',
    tag: 'Industrial Automation',
    imageSrc: '/images/project-1.jpg',
  },
  {
    title: 'Precision Turbine Components — BHEL',
    description: 'Manufactured 2,000+ high-precision turbine blades with ±0.002mm tolerance for power generation applications, meeting AS9100 aerospace-grade standards.',
    tag: 'Precision Engineering',
    imageSrc: '/images/project-2.jpg',
  },
  {
    title: 'Smart Factory IoT System — L&T',
    description: 'Implemented a real-time IoT monitoring system across 12 production lines, achieving 99.7% uptime and 25% reduction in energy consumption.',
    tag: 'Smart Manufacturing',
    imageSrc: '/images/project-3.jpg',
  },
];

export default function Projects() {
  return (
    <section className="section section--alt" id="projects-preview">
      <div className="container">
        <SectionHeading
          label="Our Work"
          title="Featured Projects"
          description="Explore how we've helped leading organizations transform their manufacturing capabilities and achieve operational excellence."
          centered
        />

        <div className="grid grid--3">
          {projects.map((project) => (
            <Card
              key={project.title}
              title={project.title}
              description={project.description}
              tag={project.tag}
              imageSrc={project.imageSrc}
              href="/projects"
              linkText="View Details"
            />
          ))}
        </div>
      </div>
    </section>
  );
}
