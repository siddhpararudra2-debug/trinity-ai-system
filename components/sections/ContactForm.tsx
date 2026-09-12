'use client';

import { useState, FormEvent } from 'react';
import Button from '@/components/ui/Button';

interface FormData {
  name: string;
  email: string;
  company: string;
  phone: string;
  service: string;
  message: string;
}

interface FormErrors {
  name?: string;
  email?: string;
  message?: string;
}

export default function ContactForm() {
  const [formData, setFormData] = useState<FormData>({
    name: '',
    email: '',
    company: '',
    phone: '',
    service: '',
    message: '',
  });

  const [errors, setErrors] = useState<FormErrors>({});
  const [submitted, setSubmitted] = useState(false);

  const validate = (): boolean => {
    const newErrors: FormErrors = {};

    if (!formData.name.trim()) {
      newErrors.name = 'Name is required';
    }
    if (!formData.email.trim()) {
      newErrors.email = 'Email is required';
    } else if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(formData.email)) {
      newErrors.email = 'Please enter a valid email address';
    }
    if (!formData.message.trim()) {
      newErrors.message = 'Message is required';
    }

    setErrors(newErrors);
    return Object.keys(newErrors).length === 0;
  };

  const handleSubmit = (e: FormEvent) => {
    e.preventDefault();
    if (validate()) {
      setSubmitted(true);
      // In production, this would submit to an API endpoint
    }
  };

  const handleChange = (field: keyof FormData, value: string) => {
    setFormData((prev) => ({ ...prev, [field]: value }));
    // Clear error on change
    if (errors[field as keyof FormErrors]) {
      setErrors((prev) => ({ ...prev, [field]: undefined }));
    }
  };

  if (submitted) {
    return (
      <div className="card" style={{ textAlign: 'center', padding: 'var(--space-12)' }}>
        <div style={{ fontSize: '48px', marginBottom: 'var(--space-4)' }}>✓</div>
        <h3 style={{ marginBottom: 'var(--space-3)' }}>Message Sent Successfully!</h3>
        <p className="text-muted">
          Thank you for reaching out. Our team will get back to you within 24 hours.
        </p>
      </div>
    );
  }

  return (
    <form className="contact-form" onSubmit={handleSubmit} id="contact-form" noValidate>
      <div className="contact-form__row">
        <div className="form-group">
          <label className="form-group__label form-group__label--required" htmlFor="contact-name">
            Full Name
          </label>
          <input
            className="form-group__input"
            type="text"
            id="contact-name"
            placeholder="John Doe"
            value={formData.name}
            onChange={(e) => handleChange('name', e.target.value)}
          />
          {errors.name && <span className="form-group__error">{errors.name}</span>}
        </div>
        <div className="form-group">
          <label className="form-group__label form-group__label--required" htmlFor="contact-email">
            Email Address
          </label>
          <input
            className="form-group__input"
            type="email"
            id="contact-email"
            placeholder="john@company.com"
            value={formData.email}
            onChange={(e) => handleChange('email', e.target.value)}
          />
          {errors.email && <span className="form-group__error">{errors.email}</span>}
        </div>
      </div>

      <div className="contact-form__row">
        <div className="form-group">
          <label className="form-group__label" htmlFor="contact-company">
            Company
          </label>
          <input
            className="form-group__input"
            type="text"
            id="contact-company"
            placeholder="Your company name"
            value={formData.company}
            onChange={(e) => handleChange('company', e.target.value)}
          />
        </div>
        <div className="form-group">
          <label className="form-group__label" htmlFor="contact-phone">
            Phone Number
          </label>
          <input
            className="form-group__input"
            type="tel"
            id="contact-phone"
            placeholder="+91 98765 43210"
            value={formData.phone}
            onChange={(e) => handleChange('phone', e.target.value)}
          />
        </div>
      </div>

      <div className="form-group">
        <label className="form-group__label" htmlFor="contact-service">
          Service of Interest
        </label>
        <select
          className="form-group__select"
          id="contact-service"
          value={formData.service}
          onChange={(e) => handleChange('service', e.target.value)}
        >
          <option value="">Select a service...</option>
          <option value="precision">Precision Engineering</option>
          <option value="automation">Industrial Automation</option>
          <option value="quality">Quality Assurance</option>
          <option value="consulting">R&D Consulting</option>
          <option value="manufacturing">Manufacturing Solutions</option>
          <option value="management">Project Management</option>
          <option value="other">Other</option>
        </select>
      </div>

      <div className="form-group">
        <label className="form-group__label form-group__label--required" htmlFor="contact-message">
          Message
        </label>
        <textarea
          className="form-group__textarea"
          id="contact-message"
          placeholder="Tell us about your project requirements..."
          value={formData.message}
          onChange={(e) => handleChange('message', e.target.value)}
        />
        {errors.message && <span className="form-group__error">{errors.message}</span>}
      </div>

      <Button variant="primary" size="lg" type="submit">
        Send Message
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <line x1="22" y1="2" x2="11" y2="13" /><polygon points="22 2 15 22 11 13 2 9 22 2" />
        </svg>
      </Button>
    </form>
  );
}
